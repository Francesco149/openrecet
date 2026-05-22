# OpenRecet — Progress Log

Reverse-chronological log of meaningful changes. Auto-generation TBD once
the test harness has coverage metrics worth reporting.

## 2026-05-23 — mesh loader: C2 Python oracle + C3 C parser + C4 D3D8-ready mesh

Three chips on the FUN_00472836 .x mesh-loader family, building on the
C1 survey landed earlier today. 14 new unit tests (807 total from 793).
All 242 .x files in `vendor/original/xfile{,2}/` parse + build clean
under ASan + UBSan.

### C2 — Python parser oracle (commit `4af5fe3`)

`tools/extract/xfile.py` grows from a 130-line stub histogrammer to a
full recursive-descent parser (~1130 lines, stdlib-only). Templates
recognised: Mesh / MeshNormals / MeshTextureCoords / MeshMaterialList /
MeshVertexColors / Material / TextureFilename / Frame /
FrameTransformMatrix / Header. Skinning + animation templates
(SkinWeights / XSkinMeshHeader / Animation / AnimationSet /
AnimationKey) brace-skipped and counted.

Output schema (per-file JSON): `path`, `size`, `header`, `stats`,
`textures[]`, `global_materials[]`, `meshes[]` (with vertices, faces,
normals, UVs, material refs, inline materials, face_material_indexes),
`frames[]` (DFS-flat with `children_names`), `skipped_templates`.
Two modes: `--full` (default, includes all arrays) and `--brief` (just
counts + metadata, useful for corpus-wide scans).

Pinned ice01.x assertions in `--self-test`: mesh_count=1,
total_vertices=41, total_faces=30, total_normals=17, two global
materials (`xof_default` + `Material__25` with texture `w_ice.bmp`),
first vertex ≈ [-8.577065, -3.734980, -7.484766], Frame_World hierarchy
with Frame_Box01 child.

Format quirks surfaced + documented in `docs/formats/xfile.md`
(commit `d65f885`):
1. MeshVertexColors per-item separator polymorphism (`cave_dun`
   `;,` vs `boss_omu`/xfile2 `;;,`).
2. MeshMaterialList face_indexes terminator variance (`0;;` vs
   `0,0,...,0;`).
3. Material reference blocks have no interior `;` (just `{Name}`).
4. Hyphen-in-identifier stitch (`PDX02_-_Default` round-trips
   lossy but consistent — `-` drops in tokenizer, IDENTs concat).

Corpus survey output:
- `xfile/`: 223 files, 17.5 MB, 2347 meshes, 118,897 vertices,
  87,029 faces, 165 unique textures.
- `xfile2/`: 19 files, 40 MB, 86 meshes, 8747 vertices, 210
  SkinWeights instances skipped.

### C3 — pure-C parser (`src/xfile.{c,h}`, commit `6c38622`)

Recursive-descent over a hand-rolled token stream. Same template set
+ same quirk handling as the Python oracle:
- Tokenizer strips line + block comments preserving line numbers.
- Numbers: signed int/float, scientific notation, decimal-only and
  exponent-only forms accepted.
- Hyphen-stitch via instance-name reader: scans ahead through
  consecutive IDENTs until LBRACE/UUID, concatenating.
- Material reference blocks (`{Name}` with no interior `;`):
  consumes all non-RBR tokens between braces and concats — natural
  hyphen-stitch fallthrough.

Public API: `xfile_parse(data, len, path) → xfile_t*`,
`xfile_free(xfile_t*)`. `xfile_t` owns its sub-arrays; partial data
preserved on parse error (caller still must free). Memory model: lots
of small mallocs with paired frees in `xfile_free`. ASan-clean across
the full corpus.

Tests (9, all pass):
1. bad_header (16+ bytes with bad magic)
2. empty (header only)
3. bare Mesh{} (3 verts, 1 triangle)
4. Mesh + MeshNormals + MeshTextureCoords + MeshMaterialList with
   referenced + inlined Materials
5. Frame hierarchy + FrameTransformMatrix + DFS ordering
6. Hyphen-stitch round-trip (`PDX02_-_Default` → `PDX02__Default`
   both sides)
7. Vendor ice01.x pinned to same numbers as Python oracle
8. Vendor xfile/ corpus walk: all 223 parse clean
9. Vendor xfile2/ corpus walk: all 19 parse clean (skinning/animation
   silently skipped)

### C4 — D3D8-ready mesh build (`src/mesh.{c,h}`, commit `d3bf126`)

`mesh_build_from_xfile(xfile_t*) → mesh_t*` flattens the per-Mesh{}
data into a single (vertices, indices, materials, submeshes) tuple.
Each submesh = one (Mesh{} block, material) pair so the renderer can
SetMaterial+SetTexture then DrawIndexedPrimitive on a contiguous
index range — matches the engine's D3DX attribute-table model
without reimplementing ID3DXMesh.

Vertex layout: FVF 0x152 (XYZ + NORMAL + DIFFUSE + TEX1, 36 B per
vertex) — same FVF the engine's D3DXLoadMeshFromXof produces (the
literal 0x152 compare at FUN_00472836:350).

Triangulation: fan (0,i,i+1) per face. No welding pass — 3 expanded
vertices per triangle (3 unique (pos, normal, uv) tuples per tri).
Simple, visually correct, slight memory overhead vs welded. Corpus
totals: 261,087 expanded verts / 87,029 faces / 3041 submeshes.

`mesh_compute_bounds`: centroid + max-radius pass, mirrors
FUN_004aaad7. Idempotent.

`mesh_upload_d3d8` (Win32-only, behind `#ifdef _WIN32`):
CreateVertexBuffer + CreateIndexBuffer (managed pool, write-only)
+ Lock + memcpy. Not unit-tested — verified visually at render time.

Known TODOs deferred to C7:
- Frame transforms not pre-applied (vertices in Mesh-local space).
  Most shipping files have identity Frame transforms in the .x
  itself; positions come from external level/stage data — fine for
  AAB/C0A unblock.
- Per-vertex MeshVertexColors not consumed (white diffuse).
- Material ref-then-inline order assumed in MeshMaterialList
  (matches ice01.x exporter).

Tests (5, all pass):
1. empty xfile builds empty mesh
2. single triangle: 3 verts / 3 indices / 1 submesh / Red material
3. bounds_cube: 8 verts → centroid at origin, radius == sqrt(3)
4. vendor ice01.x: 30 faces → 90 verts, 1 submesh,
   Material__25 with w_ice.bmp, radius ≈ 28
5. vendor xfile/ corpus walk: all 223 build clean

### Next (C5)

`mesh_load` orchestrator equivalent to FUN_00472836. Adds:
- Path resolution + the `_s.x` "quality 1" variant + the
  `xfile2/`-or-fallback path resolution from `DAT_005c8400`.
- Global texture-name dedupe cache at `&DAT_073be908` (process-wide;
  shared across all `mesh_load` calls).
- 10 per-texture mode-flag byte side-tables at `&DAT_073cb10c..814`
  driven by texture-name prefix scans (water / shop_jutan / `_a` /
  `_s` / 6 more).
- `FUN_00471b24` equivalent (texture-load wrapper) wiring our
  `sprite_load`.

The texture-name flag matching is the engine's most arbitrary-looking
logic — natural place to do Frida-harness validation against the
retail exe per user ask 2026-05-23. Plan: hook FUN_00472836 entry
+ exit, capture (material count, texture filename list, per-texture
side-table bytes) per .x file, diff against our C output across the
242-file corpus.

## 2026-05-23 — mesh loader: survey + strategy doc (`docs/findings/mesh-loader.md`)

Opening chip on the FUN_00472836 family — the .x text-format mesh loader
that's been the Mt. Everest blocker on scene_walls AAB
(FUN_0046bf38) and scene_floor/jutan C0A (FUN_004748f8) worker-thread
bodies, and ultimately on visible scene-1 INGAME geometry. No code
landed yet; this chip just documents what we found and the path we'll
take.

### Architecture (in the engine)

- **`FUN_00472836`** (1609 B) — orchestrator. Path build → DirectXFile
  walk → per-material copy / texture dedupe / sprite_load → bounds →
  FVF clone.
- **`FUN_004c8f74`** (704 B) — d3dxof.dll dynamic load,
  `DirectXFileCreate`, registers two large custom-template decl
  blocks (XSkinMeshHeader, VertexDuplicationIndices, FaceAdjacency,
  SkinWeights, Patch, PatchMesh, FVFData, PMAttributeRange,
  PMVSplitRecord, PMInfo), then walks top-level templates.
- **`FUN_004c8baa`** (970 B) — recursive Mesh/Frame/Matrix
  dispatcher. Calls `FUN_004c75e3` for Mesh, recurses for Frame,
  multiplies for FrameTransformMatrix.
- **`FUN_004c75e3`** (4634 B) — engine's RE'd `D3DXLoadMeshFromXof`
  clone. The biggest single chunk in the family.
- **`FUN_00471b24`** (467 B) — texture-load wrapper (sprite_load
  equivalent for materials).
- **`FUN_004aaad7`** (278 B) — bounds (centroid + max radius).

Total ~8400 bytes of engine code.

### Toolchain availability (mingw-w64-i686 13.0.0)

- `<dxfile.h>` + `libd3dxof.a` — DirectXFile available.
- `libd3dx8d.a` only — **no D3DX8 headers**, so we can't link
  `D3DXLoadMeshFromXof` directly.
- D3DX9 exists but targets D3D9 device interfaces, useless to us.

### Strategy decision: custom text parser, no D3DX

- Skip d3dxof + D3DX8 entirely. Write a pure-C `.x` text parser.
- Upload to raw `IDirect3DVertexBuffer8` / `IndexBuffer8` directly,
  no `ID3DXMesh` wrapper.
- Static meshes only at first — `xfile/` (223 files, 17 MB, no
  skinning, just Mesh / MeshNormals / MeshTextureCoords /
  MeshMaterialList / Material / TextureFilename / Frame /
  FrameTransformMatrix). Skinning + animation in `xfile2/` (19
  files, 40 MB) defer to character-rendering work months out.
- Trade-off: not byte-identical to FUN_004c75e3 (4.6 KB of D3DX8 we
  skip). Acceptable per `openrecet_constraints.md` — project goal
  is drop-in, not byte-identical.

### Corpus survey (via existing `tools/extract/xfile.py --scan`)

- 100% of files are `xof 0303txt 0032` (no bin/tzip/bzip variants).
- 242 files, 57 MB total. 1.6 M lines of text.
- Top templates in `xfile/`: Material 5780, TextureFilename 2734,
  FrameTransformMatrix 2610, Frame 2578, MeshMaterialList 2347,
  MeshNormals 2347, Mesh 2323, MeshTextureCoords 2071,
  MeshVertexColors 1860.
- `xfile2/` adds SkinWeights 210, XSkinMeshHeader 35, Animation 485,
  AnimationKey 1455, AnimationSet 12.

### Chip plan (smallest-first)

1. **this doc** ← here.
2. Python parser oracle (`tools/extract/xfile.py` flesh-out) — emit
   per-file JSON (vertices/faces/materials/textures/transforms).
   Validate format quirks across all 242 files.
3. C parser skeleton `src/xfile.{c,h}` — static-mesh only, tested
   against the Python oracle on 223 `xfile/` files.
4. `src/mesh.{c,h}` D3D8 upload — vertex/index buffers, FVF 0x152,
   bounds, sprite_load integration.
5. `mesh_load` orchestrator (FUN_00472836 equivalent) — texture
   dedupe global + per-texture mode-flag side-tables.
6. Wire AAB + C0A worker bodies via `mesh_load`.
7. Scene-1 render path — Mt. Everest, ports as separate roadmap
   items.

Full discussion + struct layouts + parser grammar in
`docs/findings/mesh-loader.md`.

## 2026-05-23 — scene_buy: B13 secondary inner-body wired (page-indexed)

Sixth of the 9 secondary worker-thread inner bodies — sibling to AE8
(landed earlier today). `src/scene_buy.{c,h}` extended to handle BOTH
bodies; per-page state promoted from page-0-only globals to 50-element
per-page arrays.

### What changed

`g_scene_buy_page0_valid` / `_count` / `_names` / `_sprites` removed in
favour of:

- `g_scene_buy_current_page` — engine `DAT_0730b56c` (selector read by
  B13). Range [0, 50); engine also uses -1 as "no page" sentinel.
- `g_scene_buy_valid[50]`
- `g_scene_buy_count[50]`
- `g_scene_buy_names[50][10][256]`  (125 KiB BSS)
- `g_scene_buy_sprites[50][10]`     (8 KiB BSS)

AE8 still reads page 0 unconditionally; B13 reads
`g_scene_buy_current_page`. A new `scene_buy_page_dispatch` helper
factors the shared dynamic loop.

### B13 body (FUN_0047333b @ 0x47333b, 145 bytes)

Single-phase: same as AE8's phase 1 but page-indexed. Gated on
`(valid[page] != 0 && count[page] != 0)`. Iterates `count[page]` times
reading from `names[page]` → `sprites[page]`. Dims 0x200×0x200. Engine
sprintf format `bmp/%s` (.rdata @ 0x5c8680 — different address from
AE8's 0x5c864c, same literal). Engine sprite_load format flag 0x11
(dropped). **No singletons** (unlike AE8).

### Out-of-range page handling

Engine reads `(&DAT_06a63bdc)[page * 0xb19c]` with NO bounds check —
would OOB for page = -1 or page >= 50. Port clamps via
`scene_buy_page_dispatch` (page out of range → 0 dispatches, no-op).
Tests cover -1, 50, and 9999 → all no-op.

### Inner-body call shape

LAB_00452b13 (objdump @ 0x452b13..0x452b3e): bare `call 0x47333b` with
NO pre-arg push — same shape as AE8. Confirmed via disassembly.

### Wiring

`scene_buy_init` now registers BOTH bodies in one call:
`worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8, scene_buy_ae8_body)`
+ `(...SEC_BODY_B13, scene_buy_b13_body)`. main.c's init comment
updated. Two distinct Win32 wrappers: AE8 dispatches via
`sprites[0][slot]` + the two singletons; B13 dispatches via
`sprites[current_page][slot]` only.

### Validation

- `make -C tests run` → 782 passed (+10 new B13 tests; AE8 tests
  refactored to use the per-page array API: 10 → 11 AE8 tests,
  including a new `_ae8_ignores_current_page_selector` that pins the
  AE8/B13 distinction)
- `make -C src` builds both `openrecet.exe` + `openrecet-debug.exe`
- `tools/scenario-test.py boot-idle` → 3/3 bit-exact
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact

No visual change vs prior commit (both bodies dormant — no caller
wired). worker_load.h banner updated to mark B13 as WIRED.

### Deferred

- Per-page state writers (buy-phase customer arrival code) — not
  reverse-engineered yet; lands with the buy-phase scene loader.
- AAB / C0A / C96 are the remaining 3 NULL secondary inner-bodies.
  AAB + C0A both need FUN_00472836 (.x mesh loader, 1609 bytes) first;
  C96 is the world-map state machine (2067 bytes).

## 2026-05-23 — scene_buy: AE8 secondary inner-body wired (buy-phase loader)

Fifth of the 9 secondary worker-thread inner bodies to land —
`src/scene_buy.{c,h}` ports `FUN_0047329b` (151 bytes) end-to-end and
registers it as `WORKER_LOAD_SEC_BODY_AE8`. Structurally distinct from
the wall/floor/jutan/pause group: instead of a fixed N-entry .rdata
table, AE8 walks a runtime name buffer (page 0) for a dynamic count of
items, then dispatches two fixed singletons unconditionally.

### Three-phase body

1. **Dynamic per-item icon loop (page 0)** — gated on `(valid != 0 &&
   count != 0)`. Iterates `count` times reading 256-byte names from
   the per-page name buffer at `&DAT_06a5ead4`, formats `bmp/<name>`,
   and dispatches each to a sprite slot in `&DAT_073aa7e8` (stride 0x10
   = sprite_t). Dims `0x200×0x200`. Engine sprintf format `bmp/%s`
   (`.rdata @ 0x5c864c`); engine sprite_load format flag `0x10`
   (dropped — openrecet sprite_load doesn't carry format flags).

2. **Fixed `bmp/ivent/chrname.tga`** → `g_scene_buy_chrname`
   (`DAT_073cc8d0`), dims `0x200×0x200`. Always fires.

3. **Fixed `bmp/shopmode.tga`** → `g_scene_buy_shopmode`
   (`DAT_073a9580`), dims `0x400×0x200`. Always fires.

### Page-0 scope

AE8 only reads **page 0** of the per-page state — does NOT consult
`DAT_0730b56c` (current-page selector). The B13 sibling (FUN_0047333b,
next chip) is the page-indexed variant; this chip exposes page-0
state as standalone globals (`g_scene_buy_page0_valid` /
`g_scene_buy_page0_count` / `g_scene_buy_page0_names[10][256]`) and
the B13 follow-up will promote them to 50-element arrays.

### Slot count + overflow

Sprite-array per-page stride is 0xa0 bytes = 10 sprites/page
(`SCENE_BUY_SLOT_COUNT`). Engine has no bounds check; counts above 10
overflow into adjacent pages' sprite memory. Port clamps the dynamic
loop at 10 for memory safety; tests cover the clamp behaviour
(`scene_buy_ae8_dynamic_loop_count_overflow_is_clamped`).

### Inner-body call shape

LAB_00452ae8 (objdump @ 0x452ae8..0x452b13) just `call 0x47329b` with
NO pre-arg push — argument-less call, unlike B3E/B82/BC6/C0A which
push literal `1` first. Confirmed via disassembly; no fidelity issue
to fix in our port.

### Wiring

`main.c` calls `scene_buy_init(g_dev)` after `sysassets_load_all`,
before the wall/floor/jutan/pause inits. Caches the D3D device and
registers the body via `worker_load_set_sec_body(
WORKER_LOAD_SEC_BODY_AE8, …)`. Dormant until something calls
`worker_load_spawn_d3e(0)` — buy-phase scene transition will do this
once it ports.

### Validation

- `make -C tests run` → 772 passed (+12 new scene_buy tests)
- `make -C src` builds both `openrecet.exe` + `openrecet-debug.exe`
- `tools/scenario-test.py boot-idle` → 3/3 bit-exact
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact

No visual change vs prior commit (body dormant — no caller wired).

### Deferred

- B13 sibling (FUN_0047333b) — page-indexed variant; next chip.
  Will promote `g_scene_buy_page0_*` to per-page arrays.
- Per-page state writers (buy-phase customer arrival code) — not
  reverse-engineered yet; lands with the buy-phase scene loader.

## 2026-05-22 — scene_walls: B3E secondary inner-body wired (wall asset loader)

First of the 9 secondary worker-thread inner bodies to actually land —
`src/scene_walls.{c,h}` ports `FUN_0047474e` (142 bytes) end-to-end and
registers it as `WORKER_LOAD_SEC_BODY_B3E`. The body is functionally
dormant: no caller invokes `worker_load_spawn_d85()` yet (waits on the
scene-1 stage transition to port), but the registration plumbing is
proven by the new unit tests + non-regression on existing scenarios.

### Module

`src/scene_walls.{c,h}` — 142-byte engine fn collapsed to a 15-iteration
loop with a 1-bit predicate inverted by `param`:

- `param == 0` → load ONLY the slot whose index matches the per-stage
  wall selector (engine `*(int *)(&DAT_0451057c + DAT_0438b1e0 * 0x2dfc8)`).
  "Load the destination room's wall."
- `param != 0` → load every slot EXCEPT the selector. "Load all other
  variations for snappier room changes."

Selector is exposed as a single int32 (`g_scene_walls_selector`, BSS-zero
default) until the stage-state record (0x2dfc8 stride) ports. Range
check is implicit: out-of-range selector (e.g. boot-default 0 is in
range; -1 or 15+ is out of range) means "no slot matches" — `param=0`
loads nothing, `param=1` loads everything.

### Filename table

15 entries extracted from `vendor/unpacked/recettear.unpacked.exe` via
`tools/analyze/pe.py str 0x005ca11c..0x005ca200`:

```
kabe_sikkui.bmp, kabe_ita.bmp, kabe_hosi.bmp, kabe_umi.bmp,
kabe_moru.bmp, kabe_renga.bmp, kabe_giseki.bmp, kabe_8bit.bmp,
kabe_jya.bmp, kabe_iseki.bmp, kabe_euria.bmp, kabe_namako.bmp,
kabe_chuka.bmp, kabe_kouhaku.bmp, kabe_check.bmp
```

Engine sprintf format `xfile/wall/%s` (`.rdata` @ 0x5ca210) — `xfile/`
prefix is shared with the engine's .x mesh tree even though wall assets
are BMPs.

### Test injection

The pure-C `scene_walls_load_with(load_fn, userdata, param)` takes a
test-replaceable load callback (just `(path, slot, userdata)` — no
sprite_t in the signature, so the test build is portable without d3d8).
On Win32, the body wraps `sprite_load` against `g_scene_walls[slot]`
(15-entry sprite array). Pre-existing 699-test suite + 12 new tests
all pass (711 total).

### Wiring

`main.c` calls `scene_walls_init(g_dev)` once at boot, right after
`sysassets_load_all`, which caches the device and registers the body
via `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B3E, …)`.

### Banner update

`src/worker_load.h` per-slot inner-body table now marks B3E as **WIRED
— see src/scene_walls.{c,h}**. The other 8 slots stay NULL until their
scene loaders (FUN_0046bf38, FUN_0047329b, FUN_0047333b, FUN_004747dc,
FUN_0047486a, FUN_004748f8, FUN_00473a3e, FUN_0049de20+FUN_004735ad)
port.

### Validation

- `make -C tests run` → 711 passed (+12 new scene_walls tests)
- `make -C src` builds both `openrecet.exe` + `openrecet-debug.exe`
- Boot smoke clean (4 s; full table-loader log unchanged)
- `tools/scenario-test.py boot-idle` → 3/3 bit-exact
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact

No visual change vs prior commit (B3E body is dormant — no spawner
caller wired).

## 2026-05-22 — worker_load secondary inner-body docs + post-body fidelity fixes

Decoded each of the 9 LAB_00452* secondary thread-proc inner bodies via
objdump (Ghidra missed them as code labels inside the asset-load worker
region). Recorded the call-target map in `src/worker_load.h`'s banner so
the future scene-1 port knows what to register for each
`worker_load_set_sec_body(slot, cb)`. While verifying I found two
dormant fidelity drifts in the existing post-body switch — both fixed
here.

### Inner-body call targets (objdump @ 0x452aab..0x452cdd)

| slot | LAB         | engine inner-body call(s)                                        |
|------|-------------|------------------------------------------------------------------|
| AAB  | 0x452aab    | `FUN_0046bf38()` — sc1 inventory/chrname/icon loaders            |
| AE8  | 0x452ae8    | `FUN_0047329b()` — buy phase: per-entry + chrname + shopmode     |
| B13  | 0x452b13    | `FUN_0047333b()` — buy phase alt, per `DAT_0730b56c` page        |
| B3E  | 0x452b3e    | `FUN_0047474e(1)` — wall asset loader (param=1 inverts predicate)|
| B82  | 0x452b82    | `FUN_004747dc(1)` — floor asset loader                           |
| BC6  | 0x452bc6    | `FUN_0047486a(1)` — jutan (rug) asset loader                     |
| C0A  | 0x452c0a    | `FUN_004748f8(1)` — table asset loader                           |
| C4E  | 0x452c4e    | unnamed @ 0x435873 (FPU state init) + `FUN_00473a3e()` (pause/status assets) |
| C96  | 0x452c96    | `FUN_0049de20()` (world-map state machine) + `FUN_004735ad()` (world-map BMPs) |

All 12 targets are scene-1 (INGAME) specific — they'll wire up when the
respective scene-1 loaders port. None of the inner bodies port today;
the slots stay NULL by default.

### Fidelity fixes (dormant — no caller invokes these spawners yet)

- **Fade-kick polarity** (b3e/b82/bc6/c0a/c4e/c96, `worker_load_sec_post_body`).
  Engine pattern at e.g. 0x452b57:
    ```
    cmp [DAT_06a49980], esi    ; esi = 1
    ...
    jne SKIP_FADE
    fade_phase_out_start(0, 0x11)
  SKIP_FADE:
    ```
  `jne` is "jump if not equal", so the fade-kick is the **fall-through**
  branch — engine fires fade when `param == 1`, not `param != 1` as the
  port had. Both the code and the header banner were inverted; both
  flipped here.

- **aab audio reset** (`g_worker_sec_state_audio` in `WORKER_LOAD_SEC_BODY_AAB`).
  Engine assembly @ 0x452abd-0x452ad8:
    ```
    push $0x1 ; xor eax,eax ; pop esi    ; esi=1, eax=0
    mov eax,[handle]                      ; handle=0
    push eax                              ; push 0 as FUN_00499579 arg
    ... (zero busy_sec, now_sec; state_1c8=1)
    call FUN_00499579                     ; DAT_09643120 = 0
    ```
  Engine XORs eax to zero **before** pushing it as the arg, so
  `FUN_00499579(0)` → `DAT_09643120 = 0`. This RESETS the audio LFO
  context (read by `FUN_0049966a`'s `DAT_09643120 == 0` clause), it
  doesn't raise it. Port had `audio = 1`; flipped to 0.

### What landed

- **`src/worker_load.c`** — `worker_load_sec_post_body` switch updated:
  aab audio write 1 → 0; 6 fade-kick gates inverted `!= 1` → `== 1`.
  Per-case comments updated with engine asm refs.
- **`src/worker_load.h`** — banner gains the inner-body call-target
  table; fade-kick + audio polarities corrected; state-bytes
  description for audio updated.
- **`tests/test_worker_load.c`** — `aab` audio expected = 0; `b3e/b82/
  bc6/c0a/c4e/c96` fade-kick expected on `param==1` (test names + body
  arg latches updated accordingly); full-cycle simulations now pass
  `param=1` to trigger the fade-kick branch. **699 tests pass**
  (unchanged count — 4 tests renamed, 0 added/removed).
- **`tests/test_main.c`** — registry updated for two renamed tests.

### Verified

- `make -C tests run` → 699/699 pass.
- `make -C src` clean.
- `tools/scenario-test.py title-z-press` → 14/14 bit-exact (no
  regression on the only path that currently touches worker_load).

### Still deferred (unchanged)

- The 9 secondary inner-body callbacks themselves (now documented in
  the banner — each scene-1 loader registers its slot when it ports).
- FUN_0046c01e (d07's pre-spawn) — register via
  `worker_load_set_sec_d07_pre_spawn` when it ports.
- Render-side counter pump at FUN_004547ab L51055.
- Nowloading gate split (fidelity follow-up; still dormant since no
  secondary spawner is called yet).

## 2026-05-22 — sim guard wires worker_load to the loading overlay

Wires the per-tick "if asset-load worker is done, drop the Now Loading
overlay" behavior into the sim loop. The worker_load module had been
fully ported across three earlier chips today but its `g_worker_busy`
flag wasn't observed anywhere — the overlay gate stayed raised forever
after the first spawn. This chip closes that loop.

### What landed

- **`sim_loading_pump` / `sim_loading_pump_pure`** in `src/sim.{c,h}`:
  port of FUN_004532df (129 bytes @ 0x4532df). Four scene-effect
  counters (DAT_06a49990/94/98/9c) + one mode flag (DAT_06a499a0)
  pumped every frame the worker is busy. All five are BSS-zero on
  init; counters only advance once their starter (FUN_004532b1 etc.,
  unported) writes a positive value. Today they sit dormant — ported
  in this chip so the sim-loop guard matches the engine's control
  flow shape, ready for scene-1 render to start consuming them.
  - 990: cyclic 1..0x1f, wraps to 0 at 0x20.
  - 994: cyclic 1..(threshold-1), threshold latched by FUN_004532bc.
  - 998 mode==0: cyclic 1..0x13.
  - 998 mode!=0: monotone with ceiling 0xc.
  - 99c: pumped by FUN_004547ab (render side), not from here.

- **Per-tick busy guard** at the top of `sim_step_a`:
  ```
  font_age_tick();                  // L50362 — runs unconditionally
  if (worker_load_busy()) {         // L50363
      sim_loading_pump();           // L50364 — scene-effect counter tick
      return;                       // L50365 — skip rest of sim
  }
  nowloading_set_active(0);         // L50367 — drop the overlay gate
  ... button ring + scene dispatch + fade_tick + frame++ ...
  ```

  Two effects:
  - **Input + scene sim freeze during loading.** Button ring stops
    advancing, scene dispatch is skipped, `g_sim_frame_count` does
    not advance. Matches the engine's "no interaction while loading"
    behavior.
  - **Overlay drops the tick after the worker thread finishes.** On
    Win32, `worker_load_thread_proc` completes within milliseconds
    of CreateThread (case-1 INGAME loader callback is unregistered →
    immediate cleanup); the very next sim tick reads busy=0 and
    clears `nowloading.g_active`.

- **font_age_tick reorder.** The engine calls FUN_0047c29d at L50362,
  *before* the busy check and *before* the button ring. The prior port
  ran it after the button ring (a wrong-order port from the first
  font landing). Corrected here — glyph cache aging now ticks during
  the loading screen too, matching the engine.

- **`sim_init` clears the pump state.** All 5 counters + threshold94
  + mode reset to 0.

### Tests

10 new tests under `tests/test_sim.c` (689 → 699):
- `sim_loading_pump_pure` cold-start no-op (all-zero in, all-zero out).
- 990 cycles 1..0x1f then wraps.
- 994 wraps at threshold; threshold=0 special case (immediate wrap).
- 998 mode==0 cycles to 0x13 then wraps.
- 998 mode!=0 clamps at 0xc (monotone).
- Module-level `sim_loading_pump` drives globals.
- `sim_init` zeros all counter state.
- `sim_step_a` busy → pump fires, ring frozen, frame count NOT advanced.
- `sim_step_a` idle → nowloading gate cleared on the very tick busy
  drops to 0.

`test_sim_step_a_advances_frame_count` + `test_sim_step_a_pipes_input_into_ring`
also gained `worker_load_reset()` calls in setup — sim_step_a now
depends on worker_load state, and the existing tests would have been
fragile against cross-test contamination.

### Smoke + regressions

- 699/699 unit tests pass (was 689).
- title-z-press scenario re-blessed (10 frames re-captured): the
  Now Loading overlay now correctly drops between frames 85 and 90
  in our build, where the prior goldens had it raised through frame
  115. Frames 73/74/80/85 remain bit-exact pass (overlay still up
  during these — worker thread hadn't finished yet). 14/14 across
  3 stability runs at exact same pixel-diff counts → timing is
  deterministic under turbo mode.
- boot-idle (3/3), title-down-press (4/4) re-pass bit-exact.
- title-options (4 captures) has 479 px diff at frames 39/60 — that's
  a pre-existing regression on this branch (the audio slider in the
  golden shows "5", current local recet.ini state writes "9";
  reproduces identically against `master`); unrelated to this chip.

### Engine fidelity notes

- The engine pumps FUN_004532df TWICE per frame during loading: once
  in sim (busy branch, FUN_004536cb L50364) and once in render
  (FUN_004547ab L51055 — unconditional). Outside loading it's once
  per frame from render only. We port only the sim-side call; the
  render-side pump is observably inert (no scene-effect counter has
  a render consumer today) and will land with FUN_004547ab.
- The engine's gate-clear is `DAT_06a49958 = 0` (primary nowloading
  gate only). The secondary gate (DAT_06a49960) is cleared by its
  thread procs' cleanup tails, not from here. Our `nowloading.g_active`
  collapses both into one boolean; calling `nowloading_set_active(0)`
  here clears the collapsed bit. That's a fidelity gap that only
  bites if a secondary worker is in flight while the primary is not,
  which doesn't happen in the vendor exe's call paths today
  (secondary spawners aren't called from anywhere yet — they unlock
  as scene loaders port). A proper split lives in a follow-up chip.

## 2026-05-22 — Asset-load worker thread (secondary family, second half)

Completes the worker_load module's "second half" — the 8 secondary
spawners + 9 secondary thread procs that the prior two chips
(`worker_load: asset-load worker thread`, `worker_load: alt primary
worker`) explicitly deferred. The primary worker dispatches on
`g_scene_state` via a 17-entry jump table; the secondary family is a
zoo of 8 named spawn entries, each with its own pre-spawn writes,
post-body cleanup, and (six of nine) conditional fade-kick.

### What landed

- **8 secondary spawn entry points** in `src/worker_load.{c,h}`:
  `worker_load_spawn_d07/d3e/d85/dc1/dfd/e39/e75/eb1`. Each ports its
  matching engine FUN_00452XXX (28-78 bytes per spawner @ 0x452d07
  through 0x452eb1). Shared shape: optional per-kind "pending=2" state
  byte write, raise secondary gates (4995c+49960) via
  `worker_load_begin_secondary`, latch param into DAT_06a49980,
  CreateThread on the picked thread proc. The d3e spawner sub-dispatches
  between LAB_00452ae8 (param==0) and LAB_00452b13 (param!=0).
  FUN_00452d07 alone has a pre-spawn hook (engine calls FUN_0046c01e
  before CreateThread) — exposed as `worker_load_set_sec_d07_pre_spawn`.

- **9 secondary thread proc bodies** factored into:
  - One shared Win32 thread-proc helper (`worker_load_thread_proc_sec`)
    that takes a `body_id` and does: dispatch_sec_pure → cleanup tail →
    sec_post_body → return 1.
  - Nine thin wrappers (`thread_proc_sec_aab` through `thread_proc_sec_c96`)
    that pin the body_id for each `LAB_00452*` entry.
  - One shared `secondary_thread_cleanup` (close handle, zero handle,
    zero 4995c, zero 49960 — same shape as the engine's per-LAB_* tail,
    distinct from FUN_00452917's gated three-flag wipe).

- **Per-LAB_* post-body machinery** in `worker_load_sec_post_body` — a
  switch on `body_id` reproduces each LAB_*'s tail-specific writes:
  - `AAB` → DAT_0438b1c8=1, DAT_06a49984=1, DAT_09643120=1
    (last via the inlined FUN_00499579(1)). No fade-kick.
  - `AE8` / `B13` → DAT_0438b1cc=1. No fade-kick.
  - `B3E` / `B82` / `BC6` / `C0A` → DAT_0438b1d4=1, fade-kick if
    DAT_06a49980 != 1.
  - `C4E` → DAT_0438b1d0=1, fade-kick.
  - `C96` → DAT_0438b1d8=1, fade-kick.

  Fade-kick is `fade_phase_out_start(0, 0x11)` (FUN_0045281c, already
  ported in `src/fade.c`).

- **9 inner-body callback slots** + their getter/setter pair
  (`worker_load_set_sec_body(body_id, cb)`). All slots default NULL —
  the per-LAB_* "scene work" calls (FUN_0046bf38, FUN_00473*, FUN_00474*,
  etc.) aren't ported yet, so the bodies are no-ops until consumers wire
  in. The cleanup + post-body machinery still fires either way.

- **7 named per-kind state byte globals** exposed for observability:
  `g_worker_sec_state_1c8/1cc/1d0/1d4/1d8/984/audio` (with `audio`
  serving as DAT_09643120, written via the engine's FUN_00499579(1)
  call). Plus `g_worker_sec_param` (DAT_06a49980) for the fade-kick
  gate readers.

- **Non-Win32 spawn stubs** for the 8 spawn entry points — gates-only
  shape (mirrors how `worker_load_spawn`/`spawn_alt` already split).
  Pending-flag writes and param latching are observable from tests
  even though no thread runs.

### Tests

27 new tests under `tests/test_worker_load.c` (662 → 689):
- Body slot registration: count=9, set/get round-trip, out-of-range
  guard, NULL clear, last-write-wins.
- d07 pre-spawn round-trip.
- begin_secondary/end_secondary gate transitions.
- dispatch_sec_pure: registered cb, unregistered no-op, out-of-range.
- sec_post_body: each LAB_*'s state writes + fade-kick gate (per-body
  param!=1 → fade triggered; param==1 → suppressed).
- 8 spawn entry points: per-kind pending flag write, gate raise, param
  latch, d07's pre-spawn invocation.
- 3 full-cycle simulations end-to-end (d85→B3E with fade-kick, e75→C4E
  with 1d0 ready, d07→AAB with no fade-kick + three-flag aab writes).
- Reset zeroes all secondary state.

### Engine cross-references

Caller mapping from `docs/decompiled/all.c`:
- `FUN_00452d07` — 9+ callers (most "background load" sites).
- `FUN_00452d3e` — 2 callers in scene transitions.
- `FUN_00452d85/dc1/dfd/e39` — single iVar6-keyed dispatch at line
  86961, picking 1/2/3/4 → dc1/d85/dfd/e39 (i.e. the four `1d4`
  spawners share one caller).
- `FUN_00452e75 / FUN_00452eb1` — no callers found in decomp. Treated
  as dead code in the vendor exe; ported for completeness with the
  same shape as their siblings.

### Deferred (still part of the wider worker-system port)

- Inner-body callbacks for the 9 LAB_*'s — register via
  `worker_load_set_sec_body` as each scene's loader/post-load code
  ports (FUN_0046bf38, FUN_0047329b, FUN_00473c15, FUN_004746fc, etc.).
- FUN_0046c01e (d07's pre-spawn) — register via
  `worker_load_set_sec_d07_pre_spawn` when that lands.
- Per-tick clear of DAT_06a49958 at top of FUN_004547ab. Still a
  render-dispatch concern; unaffected by this chip.

### Smoke + regressions

- 689/689 unit tests pass.
- title-z-press scenario: 14/14 frames bit-exact.
- Both `build/openrecet.exe` and `build/openrecet-debug.exe` link
  cleanly via the Win32 build.

## 2026-05-22 — Asset-load worker thread (alt primary + close fidelity)

Follow-up chip to the first-half worker landing earlier today. Ports
the alt primary worker (FUN_00452eed + LAB_00452a6b) — sibling of the
already-ported FUN_00452cde + LAB_0045293d that shares the same primary
gates but runs a fixed 5-call body instead of jump-table dispatch — and
closes the close-helper fidelity gap that the first-half chip left open.

### What landed

- **`worker_load_spawn_alt`** — ports FUN_00452eed (41 bytes @ 0x452eed),
  structurally identical to FUN_00452cde but targeting the alt thread
  proc (LAB_00452a6b) instead of LAB_0045293d. Same primary gates raised
  (DAT_06a49954 busy + DAT_06a49958 nowloading); same CreateThread call
  shape.

- **`worker_load_thread_proc_alt`** (Win32) — ports LAB_00452a6b body
  (~74 bytes @ 0x452a6b). The engine's body is a fixed sequence:

  ```
  if (DAT_06a4996c == 0) {
      FUN_0047472c();  // pre-room-change A
      FUN_00474681();  // pre-room-change B
  }
  FUN_004746fc();
  FUN_00473c15();
  FUN_00436f97();
  <primary cleanup: close handle, busy=0, return 1>
  ```

  Collapsed into a single registered callback (`worker_load_set_alt_cb`)
  — the scene module that owns the body decides internally whether to
  short-circuit on the `DAT_06a4996c` "same room" flag. Same shape, scene
  logic stays in scene-land.

- **`worker_load_dispatch_alt_pure`** — pure-C side of the alt thread
  proc, invoked by the Win32 thread proc and by unit tests directly.
  Always returns 1 (engine LAB_00452a6b never short-circuits — no input
  to range-check).

- **`primary_thread_cleanup`** helper — extracted from the inline tail
  of both LAB_0045293d and LAB_00452a6b (engine literally repeats the
  identical 4-instruction tail at both labels). Close handle, clear
  primary busy, leaves secondary flags alone. The new alt proc and the
  pre-existing primary proc share it now.

- **Close-helper fidelity fix** — `worker_load_close` now also clears
  the secondary flags (DAT_06a4995c + DAT_06a49960) when the handle is
  non-NULL, exactly matching FUN_00452917's three-flag wipe. The
  first-half chip explicitly deferred this with `// we don't have those
  yet, so omitted` — they exist now (declared in worker_load.c, even
  though no spawner writes them yet). The secondary nowloading gate is
  served by nowloading.c's collapsed-OR `g_active` so we don't
  blanket-clear nowloading here; same observable as the engine when only
  one side is in flight, which the engine's call paths appear never to
  violate.

- **`worker_load_busy_secondary` accessor** — reads `DAT_06a4995c`.
  Returns 0 always for now (no spawner raises it yet), but exposes the
  final shape so any consumer wired today won't break when the
  secondary spawners port.

- **`worker_load_reset` extended** — now also clears the alt cb slot
  and the secondary busy flag, alongside the 17-slot table.

- **8 new unit tests** covering: secondary busy defaults to 0; alt cb
  round-trip + NULL-clears + last-write-wins; alt dispatch with a
  registered cb / without one (still returns 1) / busy-flag agnosticism;
  alt spawn on non-Win32 raises gates without dispatching; full alt
  cycle simulation matches the primary cycle (busy bounces, nowloading
  gate stays raised); reset clears the alt cb. **662 tests total (was
  654).** title-z-press scenario re-passes 14/14 bit-exact.

### Engine fidelity notes

- The engine's primary thread proc (LAB_0045293d) and alt thread proc
  (LAB_00452a6b) share an *identical* cleanup tail — the same four
  instructions repeated inline at both labels. We extract that into a
  static helper (`primary_thread_cleanup`) since it's verbatim shared.

- The engine's close-helper (FUN_00452917) clears only the secondary
  flags, not the primary. The contract appears to be "shut down any
  in-flight secondary worker; primary may still be running on a
  parallel transition". We match.

- DAT_06a4996c (the "same room" gate at the alt body's entry) is set
  by the alt's sole caller in the engine — a fade-driven room
  transition handler at the FUN_00452f16 surroundings. We don't need
  it inside worker_load: the registered alt cb is responsible for the
  internal skip-prelude decision.

### Deferred (the remaining "second half")

- **Eight DAT_06a49960-gated spawners** — the original session note
  listed six (FUN_00452d07 / d3e / d85 / dc1 / dfd / e39), but
  disassembly of the 0x4528d0..0x452f50 range turned up two more
  (FUN_00452e75 + FUN_00452eb1) past the close-helper. **Nine thread
  routines** (LAB_00452aab / ae8 / b13 / b3e / b82 / bc6 / c0a / c4e /
  c96) — original count was seven, plus the two newly-found at c4e
  and c96. Each thread proc clears all three flags
  (handle + secondary busy + secondary gate); several also call
  `FUN_0045281c(0, 0x11)` (fade kick) conditional on DAT_06a49980.
  Lands when any of those transition consumers ports.

- **Per-tick gate clear** at top of FUN_004547ab — "if `worker_busy
  == 0` then clear nowloading gate". Render-dispatch concern, lands
  with the scene-1 render port.

## 2026-05-22 — Asset-load worker thread (first half)

Worker-thread infrastructure for the scene-transition asset loader
lands. The engine spawns a one-shot worker on every cross-scene
transition that dispatches a per-scene loader callback against
`g_scene_state`; this chip ports the spawn + dispatcher + busy +
close machinery, leaving the per-scene loader callbacks unregistered
(every case is a no-op until each scene's loader ports).

Single file pair: **`src/worker_load.{c,h}` + `tests/test_worker_load.c`**.

### What landed

- **`worker_load_spawn`** — ports FUN_00452cde (41 bytes @ 0x452cde).
  Win32 build: raises busy + nowloading gates, then `CreateThread`
  on the internal thread proc which reads `g_scene_state`, calls
  `worker_load_dispatch_pure`, and cleans up. Non-Win32 unit-test
  build: raises the gates only (no real thread) so unit tests can
  observe the "busy + nowloading set" window without threading.

- **`worker_load_dispatch_pure`** — ports the 17-entry jump table
  at LAB_0045293d (~302 bytes @ 0x45293d). Engine table at 0x452a27
  isn't decompiled as a function (Ghidra leaves the LAB targets as
  raw bytes); decoded via `objdump` + a Python dword reader. Map:

  | case | target(s)                          | ported? |
  |-----:|------------------------------------|---------|
  |  0   | FUN_004733d5 + FUN_0049a3a3 (title)| no (callback) |
  |  1   | FUN_00474a9a + FUN_00436f97 (ingame)| no    |
  |  2   | FUN_0047355d                       | no      |
  |  3   | FUN_004736bd + FUN_0041edf1        | no      |
  |  4   | (engine no-op, jump to cleanup)    | n/a     |
  |  5   | FUN_0046c01e + FUN_0046bf38        | no      |
  |  6   | FUN_00473769                       | no      |
  |  7   | FUN_00473585                       | no      |
  |  8   | FUN_0049de20 + FUN_004735ad        | no      |
  |  9   | sub-dispatch on DAT_06a4997c       | no      |
  | 10   | FUN_0047347d                       | no      |
  | 11   | FUN_0045bdc2 + FUN_00473874        | no      |
  | 12   | (engine no-op, jump to cleanup)    | n/a     |
  | 13   | FUN_00473972                       | no      |
  | 14   | FUN_00473991                       | no      |
  | 15   | FUN_004739fb                       | no      |
  | 16   | FUN_004739dc                       | no      |

  Per-case wiring uses `worker_load_set_cb(N, fn)` registration so
  worker_load stays decoupled from scene-specific modules. Cases
  without a callback are no-ops — exactly matching the engine's
  cleanup-only behaviour at cases 4 and 12, and what we want for the
  14 other cases pending their loader ports.

- **`worker_load_busy`** — ports FUN_00452911 (6 bytes @ 0x452911,
  just `return DAT_06a49954`). Used by the engine at the top of
  FUN_004547ab to early-exit the per-tick render dispatch while a
  worker is still loading.

- **`worker_load_close`** — ports FUN_00452917 (38 bytes @ 0x452917).
  Closes the worker thread handle if any + zeros it. Idempotent.
  Engine also clears the secondary worker's busy + gate flags here;
  we don't have the secondary worker yet, so just the primary handle.

- **`scene_post_fade_init` re-wired** — the LOADING→INGAME transition
  now calls `worker_load_spawn()` instead of `nowloading_set_active(1)`
  directly. Same observable: the nowloading gate stays raised after
  the worker completes because the engine's per-tick "clear if worker
  done" lives at the top of FUN_004547ab (not ported yet). The
  `title-z-press` scenario re-passes 14/14 bit-exact across the
  transition window.

- **15 unit tests** covering: case count == 17, reset zeroes all
  state, begin raises both gates, end clears busy but NOT nowloading,
  callback round-trip + out-of-range guard + overwrite semantics,
  dispatch invokes registered cb / no-ops the unregistered slots /
  returns 0 for out-of-range scene_state, dispatch doesn't touch
  busy, close idempotent, spawn (non-Win32) only raises gates, and
  a full-cycle simulation of the thread proc body. **654 tests
  total (was 639).**

### Engine fidelity notes

- The engine has a latent race: `CreateThread` can return + the
  thread can start before the spawner assigns the handle to
  `DAT_06a49950`, so the thread's self-close may stale-read. Match
  preserves this; in practice real case-0..16 loaders take ms so the
  race never bites.

- The engine clears busy AFTER closing the handle (`andl $0x0` on
  `0x6a49950` THEN on `0x6a49954`). Our thread proc mirrors the order.

- Case 9's sub-dispatch on `DAT_06a4997c` (0/1/2/default) is treated
  as a single callback slot — when the case-9 loader ports, its
  callback will internally do the sub-dispatch. Same shape.

### Deferred (the "second half" of the worker system)

- **Six DAT_06a49960-gated spawners** (FUN_00452d07 / d3e / d85 / dc1
  / dfd / e39) + their **7 thread routines** (LAB_00452aab / ae8 /
  b13 / b3e / b82 / bc6 / c0a) — alternate worker family for
  non-loading scene transitions (dungeon-rest, etc.). Same close/busy
  machinery; lands when any of those transition consumers ports.

- **Alternate DAT_06a49958 worker** at FUN_00452eed + LAB_00452a6b —
  simpler routine (5 calls + cleanup) shared with the primary's
  busy/nowloading flags.

- **Per-tick gate clear** at top of FUN_004547ab — "if `worker_busy
  == 0` then clear nowloading gate". Lives in the render dispatcher,
  not the worker module. Until that ports, the gate stays raised
  after the worker completes — same observable as the previous
  `nowloading_set_active(1)` stub.

- **Per-case loader callbacks** — every case slot is unregistered.
  Each scene's loader port will end with a `worker_load_set_cb(N,
  scene_X_load)` line wired from the appropriate module init.

## 2026-05-22 — Save-back (FUN_004905a8 simplified) + settings persistence

Persistence loop closes: settings-menu slider changes now survive
across boots when the user opts in via `--save-write`.

Single commit, four pieces:

1. **`save_io_write_arena(primary, backup)`** — simplified port of
   FUN_004905a8(-1). Writes the in-memory arena (header + 100 banks)
   to both files unconditionally — matches the engine's
   no-atomic-temp+rename behaviour. The engine's full FUN_004905a8
   takes a slot index that triggers a "working-bank → arena bank"
   copy + checksum re-stamp; we don't have a working-bank scratch
   yet (no gameplay state to sync), so that branch is omitted. Pass
   `-1` in the engine for matching semantics.

2. **`scene_title_settings_apply_slider`** — each `audio_fade_set_*`
   call is now paired with the corresponding `save_header_set_*_slider`
   write. The header is the persistence source of truth; audio_fade
   is the runtime slider state synced from it at boot. Settings
   changes propagate both ways simultaneously.

3. **`--save-write` CLI flag** in main.c (default OFF). When set,
   shutdown calls `save_io_write_arena("save.dat", "_save.dat")`
   right before the rest of the shutdown chain. Default OFF so
   harness/smoke runs don't trample the user's real save with
   whatever in-memory state they ended in. Manual UX test:
   ```
   ./build/openrecet-debug.exe --save-write
   # ↓ → ↓ → A on Options → adjust Music slider, exit
   ./build/openrecet-debug.exe
   # boot trace shows the new slider value
   ```

4. **Round-trip + write tests** (4 new, 639 total). Covers both files
   written, NULL paths skipped, one-valid-one-NULL succeeds, and a
   full `set → write → clear → load → assert` slider round trip.

### Engine fidelity notes

- The engine writes both save.dat AND _save.dat in sequence — no
  atomic rename. We match. Either file is independently readable on
  next boot via save_io_try_load.
- The engine's full FUN_004905a8(N) where N != -1 has a working-bank
  merge step (DAT_044e3798 + N * STRIDE → bank[N], re-checksum). That
  scratch region (DAT_044e3798) isn't populated by anything we've
  ported yet — it's where the active in-play game writes its
  modifications. Lands with the scene-1 gameplay state machine.
- The 4 known save-back callers in the engine (FUN_004902aa,
  FUN_00450a59, FUN_004907cd, FUN_00490a05) all use either -1 (no
  bank merge) or the active slot index. The -1 path is the one
  shutdown-save-back uses.
- The recet.ini overlay was removed in the previous chip (save_io
  load); audio_fade sliders now flow exclusively through save_header.
  Combined with this chip, the full persistence loop is:
  `save.dat (boot) → save_header → audio_fade → settings_apply
  → save_header (mutation) → save.dat (shutdown)`. Clean.

### Deferred (gated on this chip)

- **Working-bank scratch** (DAT_044e3798) and the bank-merge branch
  of FUN_004905a8 — lands with the scene-1 gameplay state machine
  where actual game modifications happen. Until then, only the
  shared header (sliders) usefully persists; the per-bank dwords
  remain whatever was loaded from disk.
- **Periodic auto-save** during gameplay (engine calls FUN_004905a8
  from various scene-1 sites). Same dependency.
- **Save-slot UI** — engine has multiple save slots; our shutdown
  save-back writes the entire arena, so all 100 slots persist, but
  there's no UI to choose between them yet.

## 2026-05-22 — Save-load probe (FUN_004902fe) + title-menu unlock plumbing

Boot-time save-load probe lands. The engine reads save.dat (then
_save.dat as backup) at boot and either copies its contents into the
save arena or, if neither file is readable, leaves the fresh arena
state intact.  The title menu is then rebuilt against the (possibly
loaded) save state — CONTINUE_ANY / NEW_HAS_SAVE / CONT_HAS_SAVE /
SURVIVAL / HIDDEN_CHAR menu items now unlock based on actual bank
contents rather than the all-zero fresh save we used to assume.

Single commit: **`src/save_io.{c,h}` + `tests/test_save_io.c` + main.c
wire-up + boot-order refactor**.

### What landed

- **`save_io_try_load(primary, backup)`** — ports FUN_004902fe.
  Tries `primary` (`save.dat`) first via libc `fopen("rb")`, falls
  back to `backup` (`_save.dat`).  Three engine size buckets:
   - `0x011efce0` (modern JP) — legacy-modern path; sets
     `g_save_loaded_known_format = 1`.  Per-bank parser is stubbed
     (verbatim-copy fallback); the user's saves don't hit this size.
   - `0x00f30ae0` (ancient pre-release) — symmetric stub.
   - **any other size ≤ ARENA_BYTES** — verbatim-copy.  The
     Carpe Fulgur English Steam release writes saves at exactly
     `ARENA_BYTES = 0x011f7530` (18,838,832 bytes), so the user's
     saves land here.  Engine quirk preserved: this path does NOT
     set `g_save_loaded_known_format`.
  Each path calls `save_bank_init_all()` after the copy, which
  per-bank checksum-validates and re-inits any bank whose checksum
  doesn't match.  Returns 1 if either file was read, 0 if neither.

- **`save_io_scan_for_title_menu(out)`** — fuses FUN_0049a324 +
  FUN_0049a43d's three reads against the loaded banks into the
  `scene_title_save_t` struct that `scene_title_menu_init` already
  consumes.  Drives all four flags:
   - `has_any_score`        — any bank `[2]` > 0 (the per-bank score)
   - `has_any_adv_cleared`  — additionally `bank[0xb759] == 3`
   - `has_any_adv8_cleared` — any item in `bank[6..6+bank[0]-1]`
                              has `(item >> 6)` in `[0xd49, 0xd50]`
   - `hidden_char_unlocked` — shared-header dword 6
                              (engine DAT_056e5788)
  A safety cap bounds the item-list scan to `STRIDE - 6` dwords so a
  corrupt `bank[0]` count can't walk past the bank end (the engine
  has no such cap; we add one because the alternative is an OOB
  read across 18 MB of arena).

- **main.c boot order refactor**.  Before this chip:
  `save_bank_init_all → recet.ini overlay → audio_fade sync`.
  After: `save_bank_init_all → save_io_try_load → audio_fade sync`.
  The recet.ini mu/se overlay is removed entirely — it was a
  stand-in for save-persisted sliders until save-load ported.  The
  engine itself ignores recet.ini's mu/se at boot; we now match.
  (recet.ini's audio sliders ARE still WRITTEN by FUN_0047a804's
  shutdown save-back, deferred.)  After save_io, the title menu is
  rebuilt against the loaded save (`scene_title_menu_init` with
  scanned flags) so CONTINUE_ANY etc. appear iff the save backs them.

### Engine fidelity notes

- The engine's `DAT_095d3728` is a "skip per-bank checksum
  revalidation" hint flag, NOT a "save exists" flag.  Set only on
  the two legacy size buckets.  Title-menu unlocks are independent
  — they scan the bank contents directly.
- `save_bank_init_all` post-load behaves exactly like the engine:
  any bank whose checksum doesn't match `XOR(dwords[0..0xb7f0))`
  gets re-init'd to a fresh new-game state.  The verbatim-copy from
  disk + the per-bank re-validation together produce the same
  end-state as the engine's per-bank parse + checksum stamp.
- `g_ini.mu` / `g_ini.se` continue to be parsed (recet.ini reader)
  for future shutdown save-back; they're just not consumed at boot
  any more.
- Audio sliders now flow `save.dat → header → audio_fade` cleanly.
  The user's CF EN save ships with bgm=5 (engine default never
  adjusted), so the boot bgm slider drops from 9 (recet.ini stand-in)
  to 5.  This is the new authoritative source.

### Deferred (gated on this chip)

- **Modern JP per-bank parser** (engine FUN_004902fe lines 47-101):
  reads each bank at disk-stride 0xb7a5 dwords, validates checksum
  against the stored value, copies only 0xb78d dwords (the in-memory
  bank has additional scratch fields that aren't on disk).  Stubbed
  with verbatim copy + `save_bank_init_all` validation today.
  Lands if a vintage JP save surfaces.
- **Ancient pre-release per-bank parser** (lines 128-198): symmetric.
- **Shutdown save-back** (`FUN_0047a804`): writes recet.ini values
  + a final `FUN_004902aa` `save_clear_all` to disk.  Engine writes
  the full ARENA_BYTES verbatim — see save_bank.h's "engine call
  sites" doc block.  Deferred until the shutdown chain ports.
- **Title-screen save-slot UI** (engine `FUN_0049a59e` L213 reads
  `DAT_0438b1e0` for the active slot index): currently hardcoded to
  slot 0 in `scene_post_fade_init`.  Save-slot menu lands when the
  UI ports.

### Tests + scenarios

15 new unit tests (635 total, was 620): 8 cover the arena-scan path
(fresh-arena zero flags, score-in-bank, adv_cleared requires both
score + flag, adv8 range coverage end-to-end, header hidden-char
read, bank-99 coverage, bogus-count cap), 7 cover the disk-probe
path (no-files → 0, primary-exists → 1, fall-through-to-backup,
oversized → re-init, arena-sized verbatim copy survives the
checksum revalidation pass, known-format flag set on legacy size,
known-format flag stays 0 on fallback).

Scenarios: `title-options` re-blessed (Music slider visibly steps
from 9 → 5 because save.dat is now authoritative — the only
4-pixel-region diff in the settings panel).  Other 3 scenarios
unchanged (their captures don't enter the audio-slider readout).

Boot trace now logs:

```
save_bank: arena initialized (header magic=0x341944da, sliders se=9 bgm=5 se-b=9 slider3=1)
save_io: loaded save.dat (18838832 bytes)
save_io: title menu rebuilt — items=4 (adv_cleared=0 adv8=0 score=0 hidden=0)
audio: sliders seeded — bgm=5 se-a=9 se-b=9 (authoritative source: save_header)
```

User's save file is a fresh new-game state (gold=1000, no
adventure progress) so the menu still shows 4 base items.  When a
save with real progress lands, the same code path will surface
CONTINUE_ANY / NEW_HAS_SAVE / RANKING-with-cursor etc.

## 2026-05-22 — Now Loading overlay (FUN_00453147 + FUN_004063c7)

Next deferred scene-1 chip from the sysassets entry: the engine's
"Now Loading…" overlay. Drawn AFTER the scene render and the cross-
fade alpha quad, every frame the worker-thread gate
(`DAT_06a49958` / `DAT_06a49960`) is set. Two layers:

- A static 128×64 panel sampling the "Now Loading…" text bitmap from
  bmp/nowloading.tga's (64, 0)-(192, 64) region, drawn at screen
  position (512, 400).
- A rotating 64×64 spinner sampling the (0, 0)-(64, 64) disc graphic
  from the same texture, centred at (496, 440). Rotation accumulates
  at 0.3 rad/tick.

Three commits land the chip:

1. **`src/render_quad.{c,h}` — `render_quad_draw_rotated`** (+
   pure-C `render_quad_fill_rotated_vbuf` helper for testing). Mirrors
   FUN_004063c7 (394 bytes): writes 4 vertices to slots 0..3 of the
   static vbuf, calls `DrawPrimitiveUP(TRIANGLESTRIP, 2, …)`, resets
   the vertex counter. Pure-C inner loop computes per-corner offsets
   as `x_off = -sin(angle)*r`, `y_off = -cos(angle)*r` with
   `r = half_size * sqrt(2)` and `angle = (i/4)*2π + rotation + π/4`
   for `i` in the engine's iteration order `{0, 1, 3, 2}`. UV writes
   match the engine's hardcoded VA writes at DAT_00605220/240/260/280.

2. **`src/nowloading.{c,h}`** — ports FUN_00453147 (362 bytes) end
   to end. State module owns the alpha counter (engine
   `_DAT_06a49988`, decays 32/tick when gate off, clamped at 0), the
   rotation accumulator (engine `_DAT_06a4998c`, +0.3 rad/tick when
   gate on), and the active gate. `nowloading_render(dev)` fuses the
   tick with the per-frame draw exactly like the engine does:
   defers to `nowloading_tick()` for the pure state update, then
   either bails (gate off) or sets up alpha-blend + linear-filter
   state, binds `g_sysassets.nowloading_tga`, draws the static panel
   via `render_quad_add`+`flush`, and finishes with
   `render_quad_draw_rotated` for the spinner.

3. **`src/main.c`** wires `nowloading_render(g_dev)` into the per-
   frame render dispatch immediately after `fade_render(g_dev)`
   (mirrors FUN_004547ab L203 position). Also adds the
   `D3DRS_CULLMODE = D3DCULL_NONE` write at the top of render dispatch
   to mirror FUN_004547ab L60 — without it the TRIANGLESTRIP rotated
   quad's CCW-in-Y-down winding gets dropped by the default
   D3DCULL_CCW (the static panel survives because render_quad_add's
   triangle ordering happens to be the opposite winding). The cull-
   mode fix is broader than the spinner: any TRIANGLESTRIP drawn from
   here on inherits the correct face-direction-agnostic behaviour.

4. **`src/scene.c::scene_post_fade_init`** sets
   `nowloading_set_active(1)` after the INGAME state flip — fakes
   the engine's FUN_0049de18 worker-thread gate so the overlay
   actually draws during the LOADING→INGAME transition. The flag
   never clears in our build (no worker thread yet) so the overlay
   stays on indefinitely; that's fine while the placeholder scene_1
   render lives there too.

12 new unit tests (620 total, was 608): 5 cover the rotated-quad
vertex math (axis-aligned at rotation 0, quarter-turn corner roll,
screen_w scaling, no-counter-touch, z/rhw/specular preservation),
7 cover the nowloading state machine (reset, gate normalisation,
alpha decay clamp at 0, rotation 0.3/tick, decay-and-rotation
mutual exclusion, tick return-value contract). The D3D render path
is Win32-only and verified by the harness re-bless.

Title-z-press scenario re-blessed: 14/14 capture frames now include
the spinner+panel in the post-fade frames (90..115). Other 3
scenarios (boot-idle, title-down-press, title-options) re-pass
bit-exact unchanged — they never enter INGAME state.

### Engine fidelity notes

- The engine's render dispatch calls `SetRenderState(D3DRS_CULLMODE,
  D3DCULL_NONE)` at L60 (right after `BeginScene`), then reverts to
  `D3DCULL_CW` at L207 (after everything has drawn). We set it once
  per frame at the top of render dispatch; the revert is dormant
  because nothing in our render path relies on CW culling.
- FUN_00453147 fuses the alpha-decay tick with the render path. The
  port preserves this fusion (the render function calls
  `nowloading_tick()` internally) but exposes `nowloading_tick()`
  publicly for the unit tests.
- Engine `DAT_06a49958` and `DAT_06a49960` are kept as a single OR'd
  gate in the port (`g_active`). Every consumer takes the OR; growing
  the port to two fields can wait until FUN_0049de24 (the secondary
  gate's producer) lands.
- The engine's `_DAT_06a49988` counter feeds OTHER UI elements that
  fade out in sync with the loading overlay; the overlay itself is
  gate-driven, not alpha-driven. The counter is faithfully updated
  in our port even though no consumer uses it yet.

### Deferred (gated on this milestone)

- **Worker thread + scene asset loader** (FUN_0049de18 + LAB_0049de24
  + FUN_0049dfd2) — the producer of the gate flag. Without it, the
  overlay stays active forever in our build. Lands as part of the
  scene-1 ramp.
- **`DAT_06a4998c` continuous animation** — works while the gate is
  set. A future stop-condition (worker done) will pin rotation to the
  last computed value rather than freezing mid-frame.
- **Secondary gate `DAT_06a49960`** — set by FUN_0049de24 and several
  other load paths. Currently collapsed into the primary; teasing
  apart lands when those callers port.

## 2026-05-22 — System asset loader (FUN_00472f5d)

Next scene-1 chip: ports the engine's shared system-overlay texture
loader.  Loads the ~30 textures every post-title UI overlay consumes
— "Now Loading…" panel, save/data/item windows, character portraits,
HP/MP gauges, status effect sprites, per-category item icon pages.
None of these are drawn yet (the placeholder INGAME render is
unchanged), but they're a hard dependency for the next round of port
work: the Now Loading overlay (FUN_00453147 — uses nowloading.tga),
the inventory windows, and the scene-1 HUD all consume one or more
of these.

Single commit: **`src/sysassets.{c,h}` + `tests/test_sysassets.c` +
wire-up in `src/main.c`**.

The module exposes:

- `g_sysassets` — Win32-gated struct of named sprite slots, one per
  engine `.data` global at &DAT_073aa188 / &DAT_073d9fe0 / &DAT_073cc770
  / etc.  Three loop-loaded sub-arrays: `chara_variants[3]` for
  `bmp/chr/chr%02d.bmp`, `item_icons[100]` for per-category icon pages,
  and the 20 single-load entries.
- `sysassets_load_all(IDirect3DDevice8 *dev)` — calls `sprite_load`
  for each filename in source order, matching FUN_00472f5d L27..L61.
  Per-category icon pages are loaded only for categories that have at
  least one valid item record.
- `sysassets_unload_all()` — releases every D3D texture.  Safe to
  call on a zero-init struct; safe to call repeatedly.
- `sysassets_compute_icon_sizes(items, out)` — pure helper that
  reproduces the per-category page-height math from FUN_00472f5d
  L73..L97: count valid records per category in pass 1, then return
  `max(64, ceil(count_per_cat / 8) * 32)` for each category that has
  any items.  Exposed for tests (the loader's only non-trivial math).

Wired into `src/main.c` at boot, immediately after
`scene_title_load_assets(g_dev)` — the same relative position
FUN_00472f5d holds in FUN_0047b29e (the title-bootstrap chain) at L233.
The post-device-reset reload site at FUN_004547ab L231 is deferred
until D3D8 lost-device handling lands.

New boot trace line confirming the load (against vendor data):

```
sysassets: 55 textures loaded (static=20 chara=3 item_categories=33/33)
```

20 static + 3 chara variants + 33 item categories (one per
populated 100-id band in item.txt: 100s/200s/300s through 5400s).

### Engine fidelity

- Asset filenames extracted via `tools/analyze/pe.py str` at
  0x005c84c0..0x005c8634; ordered identically to the engine's source
  order so the load trace lines up with the original on-the-wire.
- Texture (w, h) hints recovered directly from the engine's per-call
  literals (e.g. nowloading.tga is `0x100 × 0x40`). `sprite_load`
  doesn't yet resample — every audited asset ships at native
  resolution — so the hints are stored on the sprite but unused
  today.
- Chara portrait sub-loop uses a BSS-zero size table at &DAT_0438cec8
  on a fresh boot (the chara-select scene populates it later).
  Port matches by passing (0, 0) to `sprite_load`, which loads at
  native resolution.  When the chara-select port lands, the table
  will be wired in and the hints become live.
- Item-icon loop: the engine uses one register (iVar5) as both
  "max category seen" tracker and the temporary that holds the
  computed page height — overwriting itself mid-iteration.  Our port
  uses two named variables for clarity (semantically identical:
  records are sorted by item_id and hence by category, so the
  max-tracker fires the load exactly once per category).
- Two sub-blocks intentionally deferred (both BSS-zero on the boot
  path, so dormant):
  - 20-dword zeroing loop at &DAT_068dccc4 (stride 40 bytes) — only
    needed on the device-reload path, where the consumer state needs
    a reset.  First-touch-is-zero covers our boot.
  - `DAT_0076b948`-gated array load (custom-image icon pages added
    by FUN_00474f4f — vendor never populates them).

12 new unit tests (608 total, was 596).  Tests cover the pure
icon-size helper across empty/single/eight/nine/seventeen/large
counts, invalid records, multi-category, out-of-range categories,
max-category tracker semantics, and the two `_Static_assert`-like
constant pins (chara variant count, item category slot count).
Win32 surface (sprite_load → IDirect3DTexture8 upload) is not
testable from the host driver — same constraint as
test_scene_title.c.

All 25 captures across 4 scenarios re-pass bit-exact.  No visible
change today (assets load but aren't drawn yet).

### Deferred (gated on this milestone or related)

- **Now Loading… overlay** (FUN_00453147, 362 bytes) — uses
  `g_sysassets.nowloading_tga` plus a rotated quad render
  (FUN_004063c7, 394 bytes).  Gate flag (`DAT_06a49958`) is set by
  the worker thread; without the worker, the overlay stays invisible.
  Next chip candidate: port the overlay + fake the gate flag for the
  17-tick post-fade window.
- **Device-reset reload path** (FUN_004547ab L228..L231) — re-calls
  FUN_00472f5d after `IDirect3DDevice8::TestCooperativeLevel` returns
  `D3DERR_DEVICENOTRESET`.  Lands with general lost-device support.
- **Chara size table producer** (chara-select scene) — populates
  &DAT_0438cec8 so the chara portrait loads use real dimensions.
- **Custom-image array** (`DAT_0076b948` path, FUN_00474f4f) — for
  user/modder-added portraits; not present in vendor data.

## 2026-05-22 — Save-arena init (FUN_004901c2 + FUN_0049001c)

Past-the-placeholder foundation chip: ports the engine's full save
arena bootstrap + per-bank fresh-state initializer.  Largest
single-module port this session (~1150 lines of C + 14 unit tests),
and the gating dependency for all further scene-1 work — scene-1 sim
+ render fns read from the 188360-byte-per-slot save bank, which now
exists with correct field constants.

Two commits:

1. **`src/save_bank.{c,h}` + tests/test_save_bank.c** — pure-C module
   owning the full 18.84 MB arena (shared header + 100 × bank).
   Public API: `save_bank_init_all` (= FUN_004901c2), `save_bank_init_one`
   (= FUN_0049001c), checksum verify/stamp, named-field constants
   (gold=1000, week=7, rank=100, SE/BGM/SE-B/slider3 defaults
   9/5/9/1), and shared-header slider get/set accessors.

   Three engine helpers folded in:
   - `FUN_0048ff93` (starter items) — encoded slot IDs from
     STARTER_ITEMS[8][5] (DAT_005cf788, 40 dwords extracted via
     pe.py) written into per-chara inventory windows.
   - `FUN_0048ffd9` (starter flag-pairs) — 10 pairs per chara from
     STARTER_FLAG_PAIRS[8][10][2] (DAT_005cf864). **Engine quirk
     preserved verbatim**: the table is undersized (64 valid pairs
     of 80 declared); the last 16 overrun into adjacent .data
     strings ("wb"/"_save.dat" file-mode literals).  Dormant in
     vendor because NEW GAME only reads chara[0]'s row.
   - `FUN_0047a8c0` (per-chara stat interpolation) — pure-C
     equivalent of the FPU sequence at 0x47a8c0 in the unpacked
     binary, formula `value = base + (lv100 - base) * level / 100`
     reading from `g_chara[]` (populated by chara.txt parser).

   14 unit tests pin arena geometry, slider defaults, idempotent
   re-init, checksum tamper detection, RNG state advancement (1 LCG
   draw per chara × 8 charas × 100 banks = 800 draws at init_all),
   plus two overlapping-write quirks documented via assertion:
     - the named mini-block at bank[0xb388..0xb38d] (constants
       3,3,1,0,0,1) are DEAD writes — fully overwritten by
       apply_starter_flag_pairs' span [0xb384..0xb397].
     - chara record dwords [0xb..0xf] are DEAD writes — overwritten
       by apply_starter_items' encoded slot IDs (id<<6 | 0x20).
   Total 596 unit tests (was 582).

2. **`src/main.c` + `src/scene.c` wire-up** — promotes save_bank
   from "compiled but unused" to live in both init paths:

   - **Boot:** `save_bank_init_all()` runs immediately after
     audio_init, replacing the prior recet.ini-only slider seed.
     Engine defaults (9/5/9/1) populate the shared header first;
     recet.ini's mu/se values then overlay on top to preserve user
     preference until save-load (FUN_004902fe) ports.  audio_fade
     sliders are then synced from the header so the per-channel
     apply hook draws from one source of truth.  The engine's
     FUN_00499583 callback (BGM SetVolume re-apply on header init)
     is wired via `save_bank_set_header_init_hook` + a tiny
     `save_bank_apply_bgm_via_audio_fade` bridge so save_bank
     doesn't link against audio.c.

     New boot trace lines:
     ```
     save_bank: arena initialized (header magic=0x341944da,
                sliders se=9 bgm=5 se-b=9 slider3=1)
     audio: sliders seeded — bgm=9 se-a=9 se-b=9 (save_header
                overlay from recet.ini bgm=9 se=9)
     ```

   - **NEW GAME post-fade:** `scene_post_fade_init()` now calls
     `save_bank_init_one(0)` between the LOADING and INGAME state
     writes — mirrors FUN_0049a59e L213's `FUN_0049001c(active_bank)`.
     Slot index hardcoded to 0 until save-slot UI lands (matches
     engine on a fresh boot with DAT_0438b1e0 BSS-zero).

All 25 captures across 4 scenarios (boot-idle, title-down-press,
title-options, title-z-press) re-pass bit-exact.  The placeholder
INGAME chip is unchanged frame-for-frame — bank-0 reset writes to
memory no consumer yet reads.

### Engine fidelity notes

- The chara loop and FUN_0047a8c0 collapse: the engine calls
  FUN_0047a8c0 INSIDE the 8-iter chara loop, but FUN_0047a8c0 itself
  walks all 8 records each call — 7× redundant work.  Our port
  collapses to one post-loop call (same final memory state).
- One RNG step is consumed per chara record per bank, faithfully
  reproduced via `rng_next15()`. Net result: 800 draws per init_all.
- The 100-iter scratch loop at bank offset 0x9e78 is a no-op given
  the preceding memset — kept as a doc comment, not a runtime loop.
- The conditional carry-over branch gated on DAT_005c80ac is
  skipped — no upstream sets it pre-NEW-GAME, so the engine takes
  the false branch at first boot too.

### Deferred (gated on future ports)

- **Worker thread + asset loader** (FUN_0049de18 chain) — still
  blocks the Now Loading… overlay and real scene-1 init.
- **Now Loading… overlay** (FUN_00453147) — gated on DAT_06a49958 /
  06a49960, which only the worker thread sets.
- **Scene-1 sim + render** — FUN_004547ab state==1 branch (6 render
  fns: FUN_0045bbf9 / 0040a765 / 00417504 / 0045404b / 0040c962 /
  004358cc / 00453d9c).  Mt. Everest scope.
- **save-load (FUN_004902fe)** — 682 bytes; reads save.dat with
  format migration (older 0x011efce0 vs newer 0x011f7530 layout);
  unlocks CONTINUE_ANY title menu items.
- **UI scratch resets** (FUN_004060ff/4682d0/452917/etc) — small
  named-global setters; deferred until their consumers (scene-1
  render path) port, otherwise dead code.

## 2026-05-22 — Post-fade scene transition + placeholder INGAME render

Past-title-fade-out chip — first time openrecet shows anything other
than the title screen. After NEW GAME, the screen now transitions
through the black fade-OUT to a placeholder dark-navy clear with a
debug label, rather than hanging on solid black forever.

Three pieces:

1. **`src/scene.{c,h}` — `scene_post_fade_init()`** — collapses the
   engine's `DAT_0438b1c0 = 8; FUN_0049de18(); DAT_0438b1c0 = 1;`
   sequence at FUN_0049a59e L64-77 into one call. Engine writes
   LOADING then INGAME within the same sim tick so no observer ever
   sees LOADING mid-flight; the same-tick INGAME write is the
   observable endpoint. Also kicks `fade_phase_out_start(0, 0x11)`
   (FUN_0045281c) at FUN_0049a59e L235 polarity, so the alpha quad
   ramps phase-(-1) over the next 17 sim ticks, revealing the
   destination scene. Save-bank reset + UI-scratch reset
   (FUN_004060ff / 4682d0 / 452917 et al, ~150 lines of decomp)
   intentionally deferred — none of their consumers are ported yet,
   so writes would land on unread globals.

2. **`src/scene_ingame.{c,h}` — placeholder INGAME renderer** — clears
   to `0xff203050` (dark navy, intentionally distinct from the title
   clear `0xff17f0ff`) plus two `font_draw_text` lines so the
   scene-state transition is visually unambiguous. Replaces with the
   real engine's per-stage palette clear + scene-1 render functions
   (FUN_0045bbf9 / FUN_0040a765 / FUN_00417504 / FUN_0045404b /
   FUN_0040c962 / FUN_004358cc / FUN_00453d9c) as they port one
   subsystem at a time.

3. **`src/scene_title.c` + `src/main.c` — wire-up** — scene_title_sim
   calls scene_post_fade_init() when fade_is_done() returns 1
   (replacing the prior bare `g_scene_state = LOADING` write).
   render_dispatch in main.c picks the per-state clear color and
   routes to scene_ingame_render when scene_state == INGAME. The
   old "holding on black" log line is replaced with "menu item N →
   INGAME (placeholder)".

`tests/test_scene.c` adds 4 unit tests covering the transition
endpoint, substate clear, and fade-phase flip. 582 tests total
(was 578).

`tests/scenarios/title-z-press/scenario.yaml` extended from 11
captures (last at frame 95, max_frames=100) to 14 captures (last at
frame 115, max_frames=120) covering the new fade-IN → placeholder
arc:

| frame | g_fade_counter | phase | alpha | scene_state | visual |
|------:|---------------:|------:|------:|------------:|--------|
| 90 | 1 | -1 | 255 (clamped) | INGAME | solid black (quad fully opaque) |
| 92 | 3 | -1 | 238 | INGAME | placeholder showing through faintly |
| 100 | 11 | -1 | 119 | INGAME | placeholder ~50% visible |
| 108 | 0 | 0 | (no quad) | INGAME | clean placeholder visible |
| 115 | 0 | 0 | (no quad) | INGAME | steady-state |

openrecet golden re-blessed (14/14 bit-exact on re-run). Retail
golden also re-blessed at the new frame indices, but cross-target
divergence is by design: retail shows solid black with a faint
"Now Loading…" overlay (FUN_00453147) at frames 92-115 because the
worker thread is loading scene-1 assets; ours skips that thread and
jumps straight to the placeholder. Captured at
`runs/comparisons/title-z-press/sidebyside.png` for visual reference.

All other scenarios (boot-idle / title-down-press / title-options)
re-pass bit-exact.

### Deferred — gated on this milestone

- **Save bank init** (FUN_004901c2 + FUN_0049001c + the ~150-line save-
  bank reset chain inside FUN_0049a59e L64-211) — needed before any
  scene-1 sim/render reads from the 188232-byte bank. The audio
  slider defaults at DAT_056e5774/_5778/_577c (9/5/9) are nominally
  set here, but our audio_fade defaults of 9/9/9 + recet.ini override
  cover the visible behaviour today.
- **Now Loading… overlay** (FUN_00453147) — gated on DAT_06a49958 /
  06a49960 BSS-zero flags, which only the loader worker thread sets.
  Lands with the worker-thread port.
- **Worker thread + asset loader** (FUN_0049de18's downstream — the
  engine's CreateThread / LAB_0049de24 / FUN_0049dfd2 chain that
  loads scene-1 BMPs/TGAs/.x meshes). Big chunk.
- **Scene-1 sim + render** — actual gameplay. Mt. Everest scope. The
  placeholder gets replaced one render fn at a time as these port.

## 2026-05-22 — Title fade-out lands (port of the RE writeup)

Acted on the title-fade-out findings doc — three small commits:

1. **`src/fade.{c,h}` + `tests/test_fade.c`** — pure-C counter/phase
   machinery. Mirrors FUN_004526f5 (phase-1 init), FUN_0045281c
   (phase-(-1) init), FUN_004526ab (per-tick advance with the
   `duration+1` clamp on phase 1 and the reset-at-`>duration` on
   phase -1), FUN_004528b3 (done-query — `counter == duration`,
   special-cased to `counter == 0x1f` for mode 2). The 100-particle
   pre-roll inside FUN_004526f5 is omitted — vestigial, no consumer
   reads `DAT_06a48d6c` / `DAT_06a4921c`. 10 new tests (total 578).
2. **Wire-up**: `sim_step_a` tail calls `fade_tick()` (mirrors
   FUN_004536cb LAB_00453cfb line 318). `scene_title_sim`'s
   `fade_counter == 30` site calls `fade_phase1_start(0, 0x11)` —
   replacing the prior no-op increment — and when
   `fade_is_done()` returns 1, transitions `g_scene_state` to
   `SCENE_STATE_LOADING`. Render dispatch in `main.c` calls
   `fade_render(g_dev)` after `scene_title_render`. `fade_render`
   lazy-loads `bmp/system.bmp` (the 128×128 UI sheet with the (9,1)-
   (15,7) black patch + (1,1)-(7,7) white patch) on first frame and
   emits a 640×480 alpha-blended quad via the existing
   `render_quad_add` path. Alpha formula is the recovered
   `(int)(256/(duration-2) * counter)` clamped to [0,255] — NOT the
   `alpha = counter` that Ghidra produced.
3. **Snap-back removed**: previously main.c caught
   `title.fade_counter >= 0x1e` and reset both fade counter and
   select_phase so the title would reappear. Replaced with a
   one-time log when `g_scene_state` transitions to LOADING; the
   fade quad keeps drawing (counter pinned at `duration+1` = 18,
   alpha clamped to 255), so the screen stays solid black until
   --max-duration-ms or user-close terminates the process. This is
   the engine's behaviour during the gap between fade complete and
   destination scene init (the worker-thread loader hasn't run yet).

Visual verification against retail (title-z-press scenario, mean-RGB
delta vs frame 50 reference):

| frame | predicted alpha | ours dmean | retail dmean |
|-------|-----------------|------------|--------------|
| 73    | 34              | 28         | 28           |
| 80    | 153             | 124        | 124          |
| 85    | 239             | 193        | 193          |
| 90    | 255 (clamped)   | 207        | 205          |

Side-by-side comparison at
`runs/comparisons/title-z-press/sidebyside.png` — visually
indistinguishable in the fade range; retail-only per-frame px
differences are ~440-491 / 786432 (~0.06%) from non-pinned
particle/pulse jitter in the un-instrumented retail capture path.

Scenario.yaml comments updated to reflect the actual alpha schedule
(prior comments referenced 1/7/12/17, which were derived from the
Ghidra mis-decomp; correct values are 34/153/239/255).

Worker-thread loading overlay (FUN_00453147 "Now Loading…") still
deferred — it's gated on `DAT_06a49958 != 0 || DAT_06a49960 != 0`
(both BSS-zero today; only the loader worker thread sets them).
Lands with the destination scene init.

## 2026-05-22 — Title fade-out RE: corrects same-day "Deferred — big" misreading

No code change this session — purely a corrective writeup. The
"Scene-state global + title fade-out counter" entry below filed the
title→NEW_GAME fade as "DEFERRED — big" based on a wrong reading of
FUN_004526f5 + FUN_00452cde. We dug into it expecting a multi-session
port, then found the real mechanism is ~250 lines of pure C plus
existing render-quad infrastructure.

Two corrections matter for future sessions:

1. **There are no fade-out particles.** The 100-element float-vec
   tables at `DAT_06a48d6c` and `DAT_06a4921c` that FUN_004526f5
   initialises are dead writes — verified via objdump that nothing
   in the binary reads them. The 30-tick pre-roll loop touches only
   itself. The "100-particle 3D mesh fly-off running on a worker
   thread" description in the prior entry was reverse-engineered
   from the init code without checking whether any consumer existed.

2. **Ghidra mis-decomps FUN_00453e8f's alpha formula.** The decompiled
   `iVar1 = __ftol()` after a plain `(float)counter` push suggests
   `alpha = counter` (max 17 ≈ 6.7% opacity). The actual x86 at
   `0x453ed5..0x453f5b` has a `flds 0x519390 (= 256.0)` + `fdivs` that
   Ghidra dropped, so the real formula is `alpha = (int)(256 *
   counter / (duration - 2))`. For NEW_GAME's `(0, 0x11)` that's
   `256/15 ≈ 17.07` per step → full opacity at counter 15.

The off-screen render target system (`DAT_06a4999c`, FUN_00454191)
that the investigation initially fixated on is a real engine
feature, but it's used for in-game scene-to-scene transitions
(triggered via FUN_00453384 — from WndProc ESC, in-game NPC
interactions, etc.) — **not** the title→NEW_GAME fade.

Full writeup with the corrected pipeline, the asm of the missing
multiplier, the dead-particle-table provenance, and the actual port
plan: `docs/findings/title-fade-out.md`.

`title-z-press` scenario captures extended from 5 frames (0/30/35/
44/50) to 11 (+73/74/80/85/90/95) so the fade-out is now within the
captured range. `max_frames` bumped from 60 to 100. Retail goldens
re-blessed. Our goldens re-blessed too (same snap-back behavior,
just more frames captured); cross-target diff at frames 73+ is now
visible in `runs/comparisons/title-z-press/sidebyside.png`.

Updated session-start memory + this PROGRESS entry. No source files
touched. Port is filed as ~3 small commits when picked up.

## 2026-05-22 — Scene-state global + title fade-out counter

First two steps in the "past the main menu" thread. The skeleton was
hardcoded to dispatch title sim + render every frame; the engine
actually fans both halves out of `DAT_0438b1c0`. And the title scene's
A-press on NEW GAME was insta-snapping back via a `pending_action`
stub; the engine actually starts a 30-frame countdown
(`DAT_0964351c`) that gates the title sim out while a fade animation
plays in the background.

Two commits:

1. **`scene: extract g_scene_state (DAT_0438b1c0) into its own module`**
   — new `src/scene.{h,c}` owns the global + a `scene_state_set_title()`
   helper mirroring FUN_0047b29e's first two writes (`DAT_0438b1c0 = 0;
   DAT_0438b1c8 = 0;`). `prewindow_init()` now also writes 1 to the
   global, matching FUN_00451790. `sim_step_a` + `render_dispatch`
   switch on `g_scene_state` — only TITLE has a producer/consumer
   today, other states drop through. Pure refactor, no behavior change.

2. **`scene_title: port DAT_0964351c fade-out counter; NEW GAME freezes
   title`** — new `fade_counter` field in `scene_title_anim_t`. At
   `select_phase == 0xf`, codes 0/4/5 (NEW_GAME / NEW_HAS_SAVE /
   CONT_HAS_SAVE) latch `fade_counter = 1` instead of routing
   through `pending_action`. Once set, the counter ticks every
   frame; `scene_title_sim` gates all menu input + the cursor_anim
   ramp out while counter > 0 (engine FUN_0049a59e L53-77). Only
   `pulse_phase` keeps advancing — BG scroll continues during the
   freeze. `main.c` watches for `fade_counter >= 0x1e` (30 frames),
   logs "destination not ported" once per code, and snaps back for
   recovery (fade_counter + select_phase reset to 0).

Visible change in `title-z-press` golden frame 50: previously showed
the post-snap-back state (NEW GAME dim, default pulse). Now shows the
mid-freeze state (NEW GAME pinned brightly highlighted at
select_phase=0xf, frozen for 30 frames). Retail at frame 50 is
deep into the 3D particle-scatter fade-out — the visible cross-target
mismatch remains until that's ported (see "Deferred" below).

### Deferred — scene fade-out particle animation (FUN_004526f5 +
FUN_00452cde thread)

The actual engine fade is a 100-particle 3D mesh fly-off running on
a worker thread (`FUN_00452cde` spawns `CreateThread` → `LAB_0045293d`,
ticks `DAT_0438bf78` once per frame). Particles are textured 3D
quads with per-particle position+rotation transforms; the back buffer
gets captured to a texture then re-rendered as fly-off tiles over
~17 ticks. `FUN_00452917` is the thread *cleanup* (CloseHandle), not
the per-frame tick as the function name might suggest.

Not a one-session task. Needs thread plumbing, back-buffer→texture
capture, 3D particle quad renderer with per-particle transforms, and
`FUN_004528b3` completion polling. Filed under "future" for now —
the existing snap-back covers the UX gap.

### Other deferred (NEW GAME destination)

`FUN_0049a59e` lines 65-200 — the post-fade NEW GAME init block —
reads/writes the 188448-byte save bank at
`DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8`, calls FUN_004060ff /
FUN_004682d0 / FUN_00490e56 (init-from-scratch) / many per-slot
resets, then transitions `scene_state` through 8 (LOADING) to 6
(game world entry, via `FUN_00490e16`). Save bank format port +
in-game scene renderer are both Mt. Everest scope from here.

568 unit tests pass; all 4 scenarios capture bit-exact.

## 2026-05-22 — Harness turbo mode (frame-limiter bypass + silent audio)

Both the retail Frida agent and `openrecet.exe` gain matching `--turbo`
and `--silent-audio` flags. Together they let `tools/scenario-test.py`
run scenarios at host-CPU speed (no Sleep) while keeping the engine's
internal wall-clock advancing at exactly the 60 FPS budget per loop
iteration — so animations / fades / RNG all stay consistent with what
they'd be at 60 FPS, just compressed in wall time. Goldens regenerated
under turbo are bit-exact against the non-turbo goldens.

Measured speedup on `title-z-press` retail capture: 1705 ms → 854 ms
(~2x). Larger scenarios benefit proportionally — the savings scale
with the number of "idle" frames between capture anchors. `boot-idle`
is already short enough that startup overhead dominates; gain is small
there but still positive. `--turbo` works under both
`--input-trace-replay` (no change — replay already runs at host speed)
and free-running mode.

### What landed

- **`tools/frida/openrecet-agent.js`** — `installTurboHooks()` replaces
  `FUN_0047be2f` (the QPC ms reader) with a `NativeCallback` that
  returns a virtual clock, and attaches `FUN_0047be92` (dispatcher)
  entry to bump that clock by `g_turbo_step_ms` (default 17) per call.
  Engine's `delta_thirds` is always 51 ≥ 50 (the 60 FPS threshold), so
  the sim+render branch fires every loop iteration with no Sleep.
  `installSilentAudioHook()` waits for `FUN_00498ef4` exit, reads
  `DAT_09643108` (BGM AudioPath), and `Interceptor.attach`'s its
  `vtable[5]` (SetVolume) to rewrite `lVolume → -10000`. All three
  audio paths share a vtable so one hook silences BGM + SE-A + SE-B.
  RPC `init` accepts `turbo` / `turbo_step_ms` / `silent_audio`.

- **`tools/frida_capture.py`** — `CaptureConfig` gains `turbo` /
  `turbo_step_ms` / `silent_audio`; `run_capture` + CLI plumbed
  through (`--turbo`, `--turbo-step-ms`, `--silent-audio`).

- **`tools/scenario-test.py`** — `--turbo` and `--silent-audio` flow
  to both `run_scenario_capture` (openrecet) and
  `run_scenario_capture_retail` (Frida). `--target both` honors them
  on both halves.

- **`src/tick.{c,h}`** — `tick_set_turbo(enabled, step_ms)` /
  `tick_turbo_enabled()`. When enabled, `tick_step_win32` feeds the
  pure-C dispatcher a virtual clock advancing by `step_ms` per call
  and skips Sleep on `TICK_RESULT_DELAYED`. Pure-C
  `tick_step_with_now` unchanged — the speed-table math and state
  machine are byte-for-byte identical regardless of clock source.

- **`src/audio.{c,h}`** — new `silent_audio_apply_hook` function
  matching `audio_fade_apply_hook_t`'s signature; clamps every
  forwarded centibel to `AUDIO_FADE_SILENCE_CENTIBEL` (-10000)
  before calling `IDirectMusicAudioPath_SetVolume`. Game's audio
  code (PlaySegmentEx, fade math, segment-state queueing) runs
  untouched.

- **`src/main.c`** — `--turbo` and `--silent-audio` parsed in
  `parse_cmdline`. Turbo applied right after `tick_init()`; silent
  audio replaces the default apply hook right after `audio_init`.

### Smoke-test results

- openrecet `boot-idle --turbo --silent-audio`: 3/3 bit-exact, 1.3 s.
- openrecet `title-z-press --turbo --silent-audio`: 5/5 bit-exact,
  1.7 s.
- retail `boot-idle --turbo --silent-audio`: 3/3 bit-exact, 0.8 s.
- retail `title-z-press --turbo --silent-audio`: 5/5 bit-exact, 0.8 s
  (vs 1.7 s without turbo — ~2x).
- `boot-idle --target both --turbo --silent-audio`: 6/6 bit-exact,
  side-by-side renders.
- 568 unit tests pass.

### Caveats

- Turbo + Frida currently lets the retail process linger ~1 s after
  scenario completion (still inside `device.kill`'s timeout). Not
  related to turbo — same pre-turbo behaviour — but the speed-up
  makes it more noticeable as a fraction of total run time. The
  belt-and-braces `tasklist | grep -i recettear` after a batch
  remains a good idea.
- DirectMusic doesn't love being clocked at 200+ fps; that's exactly
  why `--silent-audio` is recommended alongside `--turbo`. Without
  silencing, the audio backend may drop / glitch (cosmetic — game
  state stays correct because the audio fade math drives off engine
  ticks, not wall time).

## 2026-05-22 — Phase B input injection

The retail-capture pipeline now replays the same sparse JSONL trace
Phase A does, so `tools/scenario-test.py --target retail <name>` drives
the real game through the scenario's input sequence instead of capturing
an idle title screen. Unblocks every future retail golden capture that
needs menu navigation.

### What landed

- **`tools/frida/openrecet-agent.js`** — added `g_input_trace` /
  `g_input_trace_i` / `g_input_force_active` / `g_input_last_forced`
  globals. `installInputHook`'s onLeave now advances a monotonic
  cursor through every entry with `frame <= current_frame`, applies
  the sticky mask via `writeU16` to `DAT_073dddd0`, then re-reads
  for the `input_state` event so the recorded trace reflects what
  the engine actually saw. `init({input_trace, force_input})`
  accepts the trace as `[{frame, mask}, ...]` from the driver.

- **`tools/frida_capture.py`** — `CaptureConfig` gains
  `input_trace_path` + `force_input` fields; `_run_capture_impl`
  loads the JSONL (tolerating `#` comments to match
  `src/input_trace.c`), passes through to the agent. CLI adds
  `--input-trace` / `--force-input` for ad-hoc replay.

- **`tools/scenario-test.py`** — `run_scenario_capture_retail`
  always enables injection, pointing at the scenario's existing
  `trace.jsonl`. Old "no input replay yet" docstring removed.

- **`tests/scenarios/title-down-press/`** — new scenario: DOWN
  press at frame 30, cursor steps NEW GAME → MINIGAME. Strictly
  more visible than `title-z-press` in thumbnail-sized contact
  sheets (the tooltip text changes; the highlighted row changes),
  so eyeball regressions are easier to spot.

### Verification

- `boot-idle/golden-retail/` re-blessed: 3/3 frames, no behavioral
  change vs the previous bless (no input → injection is a no-op).
- `title-z-press/golden-retail/` re-blessed: agent.log records all
  three trace transitions (0→0x10→0); engine fires `se_play` slot 7
  at frame 30 (menu-confirm sound), and the NEW GAME row brightens
  through frames 30→44 (hottest diff rows 316-328 in a per-pixel
  delta vs frame 0). User-confirmed visual: NEW GAME button
  brightens through the select_phase ramp on the right column of
  a side-by-side render.
- `title-down-press/golden-retail/` blessed: cursor steps from
  NEW GAME to MINIGAME between frames 0 and 30, tooltip text
  on the left swaps, both visible at thumbnail size.

### Out of scope (deferred)

- **RNG / clock pinning.** Retail still runs at real wall-clock pace
  during capture, so cross-run bit-exactness within retail (and
  cross-host portability of retail goldens) remain undetermined.
  Re-bless on each capture host until/unless this gets pinned.
- **Joystick / mouse injection.** Only the 14-bit `DAT_073dddd0`
  player-0 mask is forced. Joystick axes / mouse position would
  need separate hooks.
- **`force_input=False` regression.** Phase B+ state-forcing
  drivers (`tools/state_diff/`) already skip the capture hooks via
  `install_hooks: false`; the injection plumbing defaults to off
  so they don't accidentally inherit forced input.

## 2026-05-22 — Settings submenu render (FUN_0049c050)

The "Options" submenu now draws. Producer landed at `d34079e` two days
ago but the render was gated on the font system; with text rendering up
since `e2ded60`, the render port now lights up the panel.

### What landed

- **`src/font_draw.{c,h}`** — added `font_draw_text_centered`, port
  of FUN_0047d14c. Walks the string with `font_slot_alloc` + immediate
  `font_slot_upload` on each fresh slot, sums per-glyph advance via
  `effective_width`, then calls `font_draw_text` at
  `center_x - width/2`. The explicit upload-on-allocate matters:
  the engine's FUN_0047cbcb is atomically alloc-and-upload-if-new,
  but our pure-C split separates them — without uploading inside
  the measure walk, glyphs first-seen by the centered draw end up
  with no texture installed (font_draw_text's draw walk sees
  `is_new=0` and skips its own upload). Symptom was missing letters
  in "Clear Save Data" rendered after the row labels: every char
  already used in the labels rendered fine, but C / l / v / D —
  only first-seen in the centered draw — came out invisible. Skips
  the dead `DAT_0438b784 & 1` legacy branch of FUN_0047d14c.

- **`src/scene_title.{c,h}`** — `scene_title_settings_render_panel`
  (FUN_0049c050 port) draws the dungeonbord panel BG + 6 row labels
  + 5 slider value strings + dormant Saving overlay. Wired into the
  end of `scene_title_render` with the gate
  `cursor_anim > 0 && submenu_state == 2`, plus the two outer
  header chrome quads from FUN_0049c644 L234-244 (item_win.tga tab
  + fuki.tga OPTIONS label) at the engine's
  `x = 640 - cursor_anim*64` slide offset.

  Row layout (top-down): MUSIC / SOUND / VOICE / MESSAGE SPEED /
  UNREAD TEXT SKIP / CLEAR SAVE DATA. Numeric sliders show 0-9;
  Message Speed shows SLOW/MED/FAST; Unread Text Skip shows OFF/ON.
  Yellow on the cursor row, grey elsewhere; engine's three
  bit-twiddle inlines for the same yellow/grey pair collapsed to
  one ternary. Engine writes both `D3DTOP_ADDSIGNED=8` and
  `D3DTOP_MODULATE2X=5` back-to-back at FUN_0049c050 L35-36, second
  wins — collapsed to a single MODULATE2X write here.

  Hard-coded 6 rows because this is the title-side caller; engine
  conditionally drops to 5 when `DAT_0438b1c0 != 0` (in-game pause
  menu, FUN_0047fc44, not yet ported). Will need a scene-state arg
  when the pause menu lands.

- **`SCENE_TITLE_TEX_ITEM_WIN` = slot 7** in `scene_title_assets`.
  Asset list grew 7 → 8 (loader, tests, fixture data all updated).
  `bmp/item_win.tga` is a boot-time UI atlas in the engine
  (FUN_0047193c context=1, alongside system.bmp, savewindow.tga,
  etc.) but parked on the title-scene loader pragmatically until a
  boot-time-textures module exists.

- **`tests/scenarios/title-options/`** — new scenario covering
  DOWN×2 → A → slide-in. 4 captures at frames 0 (baseline), 10
  (OPTIONS highlighted in main menu), 39 (panel fully slid in), 60
  (held). Bit-exact against blessed goldens.

### Known visual followups (font-system class)

- ~~Lowercase glyphs render at uppercase height~~ **fixed in
  follow-up commit**. `font_draw_text` now folds the
  `(origin_x, ascent - origin_y) * fVar2` baseline offset into
  the dst rect and uses `(tex_w, tex_h) * fVar2` for the dst size,
  keeping the small-texture upload. Lowercase glyphs now sit
  baseline-aligned with proper x-height; capital letters extend
  above. Visible win across every font draw site (smoke text,
  settings menu labels + slider values). Skipped the engine's
  `(cell_inc_x, line_height)` cell-pad approach as it would burn
  ~3x more GPU memory per slot for the same on-screen result —
  the cell pad is what the engine uses but we don't need it
  given we drive baseline via the dst rect instead.

- "Clear Save Data" centering is still ~10px off on first draw
  (engine quirk — measure walk reads `effective_width=0` for fresh
  slots; engine has the same misalignment).

### Deferred (still)

- Clear-data confirm modal (row 5 + FUN_00434def) — no save IO yet.
- Filename SE feedback on row 2 inc/dec (engine quirk #50) — uses
  generic SE 0x146 in the existing sim.
- Saving overlay visuals — needs `savewindow.tga` loading + actual
  save IO before the branch ever fires. Wired through as
  `saving_flag` param but no-op'd in the render.
- In-game pause sound menu (FUN_0047fc44) — same FUN_0049c050 with
  5 rows; lands when an in-game scene ports.

## 2026-05-22 — Font system, end-to-end (FUN_0047c228 / c474 / c3a5 / c29d / cbcb / cf22 / ca05)

Seven functions, six commits, ~1700 lines of new C — the whole text
rendering pipeline now works. A scene can call `font_draw_text(dev,
x, y, str, argb, scale)` and pixels come out. Title scene now shows
"openrecet 0.1" in the bottom-left as a smoke test.

### Architecture

```
WinMain:
  font_init()              ← clears 200-slot LRU cache + texture table
  audio_init()
  font_atlas_build_win32() ← GDI builder, conditional on g_config.font_set
                             or missing ./font/fontdata.bin (drop-in path)
  font_atlas_load()        ← reads back fontdata.bin + fontidx.bin

Per-frame (sim_a):
  font_age_tick()          ← bumps age on every in_use slot

Per-glyph (in scene render path):
  font_slot_alloc(b0, b1)  ← 200-slot LRU, age-gated eviction
  font_slot_upload(slot)   ← D3D8 CreateTexture + LockRect + ARGB expand
                             (texture release on evict hooked via callback)
```

### What landed

- **`src/font.{c,h}`** — 200-slot LRU cache state. `font_init` (port
  of FUN_0047c228) zeros the slot + texture tables, seeds slot_id with
  each entry's index. `font_age_tick` (port of FUN_0047c29d) increments
  `age` on every in_use slot — engine's debug-overlay scan is dropped
  since FUN_00451874 is a release-build stub. Wired into `sim_step_a`
  after the button ring.

- **`src/font_atlas.{c,h}`** — record format (40 bytes) + GDI atlas
  builder + disk loader. The builder mirrors FUN_0047c474:
  CreateFontIndirectA at 42px / SHIFTJIS / ANTIALIASED, walks 256
  single-byte + 288 special-table 2-byte + SJIS double-byte from 0x88
  with the engine's gap-skip pattern, rasterizes each via
  GetGlyphOutlineA(GGO_GRAY4_BITMAP), pads to a 4-pixel border, applies
  5×5 radial edge dilation, writes both files. Output goes to
  **`./font/`** (not the vendor dir — fresh path so retail and
  openrecet don't fight over atlas files). Loader (FUN_0047c3a5)
  reads them back into `g_font_atlas`. Pure-C parts (record packing,
  blit, dilation) are Linux-testable; GDI driver behind `_WIN32`.

- **`src/font_alloc.{c,h}`** — codepoint→record-id lookup + 3-phase
  slot allocator. find_existing → find_free → find_evictable (age > 3).
  Release callback hook lets the Win32 layer Release the GPU texture
  on eviction without dragging D3D into the pure-C module.

- **`src/font_upload.{c,h}`** — Win32-only D3D8 texture upload. Skips
  the engine's TGA-then-D3DX dance; uses CreateTexture + LockRect with
  D3DFMT_A8R8G8B8 directly. ~150 lines less code, same on-GPU result.
  Pure-C pixel-expansion helper (`font_upload_expand_pixel`) is
  Linux-testable.

- **`src/font_draw.{c,h}`** — `draw_text(x, y, str, argb, scale)`
  port of FUN_0047ca05. Walks the SJIS string, routes each codepoint
  through alloc → upload (if new) → SetTexture + render_quad_add +
  render_quad_flush. Per-glyph dst is `(eff_w * scale*0.494,
  42 * scale*0.494)`, advance is `(eff_w - 3) * scale*0.494` — matches
  engine math. Departure: src rect uses `[0, 0, tex_w, tex_h]`
  (full texture) instead of the engine's fixed `[1, 1, 41, 41]`
  (WRAP-relying for smaller textures). Pixel-exact match isn't a
  project goal; the eyeball test is "readable text in the right place."

- **`src/scene_title.c`** smoke: draws "openrecet 0.1" at (8, 460,
  scale=1.0). Visible in the boot-idle golden, blesses applied.

- **Atlas output gitignored**: `./font/` lands under `vendor/original/`
  in dev workflow, which is already gitignored. Atlas regenerates on
  first boot of a fresh install (no `font:` in config.idx needed —
  the loader's "files don't exist" branch triggers regen with a
  default face name).

### Tests

24 (atlas builder/record) + 6 (pixel expansion) + 15 (codepoint
lookup + slot allocator) + 5 (cache init/age) + 4 (loader) = 54 new
unit tests. Total test count 568 from 514. Both boot-idle and
title-z-press scenarios pass (re-blessed with the smoke text overlay).

### Engine quirks documented

See `docs/findings/winmain-and-bootstrap.md` §"Font system" for the
full list. Highlights:

- **kanjioff polarity inversion** in FUN_0047c474: Ghidra renders the
  break check with `== 0` but the byte-level semantic must be `!= 0`
  (otherwise vendor default would skip all kanji)
- **Phantom 0x883f glyph**: first phase-1 atlas-walker iter renders
  the invalid SJIS codepoint 0x883f → GDI returns nothing → fontidx
  slot 544 = empty record. Harmless.
- **Slot-overlap return pointer**: FUN_0047cbcb returns `slot - 12`
  so `piVar4[3]` reads slot.slot_id. The 12-byte "pre-slot" region
  is actually slot[i-1]'s pad20/pad24 + the start of slot[i] —
  effective_width gets written into pad20 during upload. We give
  effective_width its own field and skip the trickery.

### Known follow-ups

- **Visual aspect**: glyphs of varying texture height get stretched
  into the engine's fixed 42-unit dst height. Text reads as
  tall-and-narrow vertical bars at scale 1.0. The engine has the
  same math — possibly the engine's textures are all sized so the
  WRAP-sampling in [1,1,41,41] produces a consistent visible glyph
  area. Worth a second look once scene text consumers (settings menu,
  shop UI) land.
- **Title menu labels are still sprite-baked** in `fuki.tga` — the
  draw_text smoke is a separate overlay, not a replacement. Wiring
  the menu items through draw_text is for a later milestone.
- **Engine variant of upload** (TGA-in-memory → D3DXCreateTextureFromFileInMemoryEx)
  isn't byte-identical to our CreateTexture+LockRect path. Doesn't
  matter for runtime visual but means the texture in GPU memory won't
  literally match the engine's. Project memory says "not byte-identical"
  so this is fine.

## 2026-05-22 — Harness: pre-resume state-forcing + first differential test (LCG + cos-curve fade)

Phase B's deferred half — calling vendor functions with forced state to
diff against our ports — lands as MVP infra plus one end-to-end test.
The two pure-math subsystems we picked first (RNG and audio_fade) both
come back **bit-exact** to retail across the full input range. No
divergence, no need for tolerance. The RPC + oracle plumbing generalises
to any future pure-fn diff (LZSS/LZW decoders, lnkdatas_hash CRC, input
mask decoder, tick scheduler).

### What landed

1. **`tools/frida/openrecet-agent.js` — state-forcing RPC surface.**
   Five new RPC methods alongside the existing capture-side hooks:
   - `readMemory(va, len)` / `writeMemory(va, hex)` — generic
     byte-window access keyed on Ghidra VAs (preferred ImageBase
     0x00400000, recomputed against actual load base on every call).
   - `readU32(va)` / `writeU32(va, val)` — primitive shortcuts; the
     two used by the LCG diff.
   - `callU32NoArgs(va)` — invoke a u32-returning, no-arg cdecl
     function via `NativeFunction`. Used to drive `FUN_005041f6`.
   - `captureFadeCentibel(slider)` — purpose-built for the audio_fade
     diff: plants a fake `IDirectMusicAudioPath` in `DAT_09643108`
     whose vtable[5] (SetVolume) is a `NativeCallback` that records the
     centibel argument before returning S_OK. Forces `DAT_056e5778`
     (BGM slider) to the requested value, calls `FUN_00499583`,
     restores both globals. Side-effect-free — host audio is never
     touched.

   Two breakages found and fixed along the way:
   - `rpc.exports` keys must be **camelCase** in JS (not snake_case as
     the original `queue_capture` / `get_frame` were). Frida-Python
     auto-converts snake_case Python method calls to camelCase before
     dispatch, so `write_u32` on the Python side maps to `writeU32` on
     the JS side. The two existing exports were silently broken from
     day 1; Phase B's driver only ever called `init` (no underscores),
     so it never tripped. Filed as a project memory.
   - `NativeFunction` rejects `'cdecl'` as an explicit ABI on x86 — the
     valid token would be `'mscdecl'`, but the platform default does the
     right thing for no-arg / void-return calls so we omit the argument.

   `init({install_hooks: false})` skips the Phase B capture hooks —
   the state-forcing tests never resume the main thread, so the
   D3D/audio/input interceptors would never fire anyway.

2. **`tools/state_diff/oracle.c` + Makefile — local "ground truth".**
   Tiny host binary linking `src/rng.c` + `src/audio_fade.c`. Stdin
   protocol:
   - `rng_seq <seed_hex> <n>` → prints `n` post-step seed values
     (raw `DAT_006023a0` state after each LCG call — directly
     comparable to what `readU32(DAT_006023a0)` reads after
     `callU32NoArgs(FUN_005041f6)`).
   - `fade_compute <slider>` → prints
     `audio_fade_compute(slider, 0)` for the BGM diff.
   Built with host gcc, no sanitizers (it's not a unit test).
   `audio_trace_emit_fade_start` stubbed in-file so we don't drag in
   the 700-line `audio.c`.

3. **`tools/state_diff/lcg_fade.py` — driver.** Spawns retail under
   Frida in `CREATE_SUSPENDED` state and **never resumes the main
   thread**. The Frida helper thread that runs the agent is independent
   of the target's threads — it can invoke `NativeFunction` calls and
   read/write process memory without any engine code executing. No
   races against `FUN_00451790` (engine particle init advances the LCG)
   or against the real audio backend. The oracle runs concurrently as a
   long-lived stdin subprocess.

### Results (cutestation.soy:27042, retail unpacked exe)

```
# LCG step (FUN_005041f6, DAT_006023a0)
  pass seed=0x00000001  (256 steps bit-exact)
  pass seed=0x00003039  (256 steps bit-exact)
  pass seed=0xdeadbeef  (256 steps bit-exact)
  pass seed=0x80000000  (256 steps bit-exact)
  pass seed=0xfffffff0  (256 steps bit-exact)
  pass seed=0x00000000  (256 steps bit-exact)

# BGM fade curve (FUN_00499583)
  pass slider=0  retail=-10000  ours=-10000  Δ=+0cb
  pass slider=1  retail= -5391  ours= -5391  Δ=+0cb
  pass slider=2  retail= -4231  ours= -4231  Δ=+0cb
  pass slider=3  retail= -3176  ours= -3176  Δ=+0cb
  pass slider=4  retail= -2245  ours= -2245  Δ=+0cb
  pass slider=5  retail= -1458  ours= -1458  Δ=+0cb
  pass slider=6  retail=  -829  ours=  -829  Δ=+0cb
  pass slider=7  retail=  -371  ours=  -371  Δ=+0cb
  pass slider=8  retail=   -93  ours=   -93  Δ=+0cb
  pass slider=9  retail=     0  ours=     0  Δ=+0cb

16 passed, 0 failed
```

- LCG: 6 seeds × 256 steps = 1536 individual u32 comparisons, all
  bit-exact. Expected — the LCG is one `imul` + `add`, no FP, no
  platform variation.
- Fade: 10 slider values, all bit-exact (the `±1 centibel` tolerance
  in the driver was never tripped). libm `cos()` and MSVC's
  `FUN_00503994` round to the same `int32` after `__ftol` truncation
  for every (slider, target=0) point on this curve.

Deterministic across re-runs.

### Follow-up candidates (same harness, same agent surface)

The plumbing is generic — any pure or near-pure ported function gets a
short driver script:

1. **`lnkdatas_hash` CRC** — call `FUN_00474f14` with arbitrary buffers
   via `Memory.alloc` + `writeMemory` + a `callU32_ptr_u32` variant.
   Targets `src/lnkdatas_hash.c`.
2. **LZSS decompress (`FUN_004349e5`)** — write a compressed buffer +
   output buffer, call, `readMemory` the result; diff against
   `src/lnk_lzss.c`. Already validated vs `recettear-repacker` Python.
3. **LZW decompress (`FUN_00434b32`)** — same pattern; diff against
   `src/bmp_lzw.c`. Already validated vs `recettear-repacker`.
4. **Input mask decoder (`FUN_0047b73c`)** — synthesize a raw
   DI keyboard buffer + joystick state + per-binding table, call,
   `readU16(DAT_073dddd0)`; diff against `src/input.c` decoders.
5. **Tick scheduler (`FUN_0047be92`)** — fixture engine ms-clock global,
   tick once, observe state advance; diff against `src/tick.c`.

The remaining audio-backend "Next steps" item (settings-menu slider
producer `FUN_0047fc44`) and the splash/title-bootstrap port don't need
state-forcing tests yet but will benefit from this surface once they
land.

## 2026-05-22 — Harness Phase B: retail capture via Frida

Phase B lands as planned at the bottom of yesterday's Phase A entry:
`tools/scenario-test.py --target retail <name> --bless` drives the
SteamStub-decrypted retail exe (`vendor/unpacked/recettear.unpacked.exe`)
through the same scenario plumbing and writes BMPs / audio.jsonl /
trace.jsonl into a per-target `golden-retail/` directory. Output schemas
match Phase A exactly so the bless + bit-exact diff path is shared.

Five pieces:

1. **`tools/frida/openrecet-agent.js`** — Frida JS agent. Hooks the
   D3D8 init wrapper (`FUN_0047ac6a`) to capture the
   `IDirect3DDevice8*` (`DAT_073dfcbc`) once it's live, then installs:
   - `IDirect3DDevice8::Present` (vtable[15]) — frame capture
   - `FUN_00499200` (BGM swap)            — `{kind:bgm_swap, track}`
   - `FUN_00499c63` (SE play)             — `{kind:se_play, slot}`
   - `FUN_0047b73c` (input poll) onLeave — reads `DAT_073dddd0`,
     emits `{kind:input_state, buttons:0xNNNN}`
   The frame number for each event is read from `DAT_073dfcfc`
   (engine global frame counter), so capture filenames match the
   scenario's `capture_frames:` list bit-for-bit.

2. **Sysmem-bounce frame capture.** First cut hit `D3DERR_INVALIDCALL`
   on `IDirect3DSurface8::LockRect` — the retail back buffer is
   *non*-lockable (no `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`). Workaround:
   `CreateImageSurface(w, h, fmt, &sys)` + `CopyRects(bb → sys)`,
   then lock the sysmem surface (lockable by construction) and copy out
   the BGRA pixels. The captured format echoes whatever
   D3DFMT_X8R8G8B8 / A8R8G8B8 the engine asked for — both are
   compatible with our BMP layout.

3. **`tools/frida_capture.py`** — Python driver. Connects to a remote
   `frida-server.exe` (default `127.0.0.1:27042`; overridable via
   `--frida-remote` or `$OPENRECET_FRIDA_REMOTE`), spawns retail
   suspended via `device.spawn()`, installs the agent + hooks, then
   resumes. Emits BMPs bit-identical to `src/main.c::capture_backbuffer`
   so the shared diff path works. Includes `ensure_frida_server()`
   helper that auto-launches `frida-server.exe -l 0.0.0.0:<port>` via
   `powershell.exe Start-Process -Verb runAs` (UAC prompt) when the
   port isn't already reachable. Server exe location pulled from
   `$OPENRECET_FRIDA_SERVER_EXE` with a sensible default.

4. **`tools/scenario-test.py --target {openrecet,retail}`**. Per-target
   golden dirs: `golden/` (openrecet, unchanged) vs `golden-retail/`
   (Phase B). Bit-exact diff within a target; cross-target diff is
   out of scope (different draw call ordering / font system — never
   bit-comparable, deferred to a future contact-sheet tool).

5. **`tests/scenarios/boot-idle` blessed under retail.** First retail
   golden: 3/3 frames captured at the engine's 640×480 back-buffer
   (window stretches it to 1024×768), audio trace caught the title
   BGM swap on frame 0 (`{track:0}`), input trace recorded the
   all-zero idle mask. Re-running without `--bless` shows 3/3
   bit-exact pass — retail's boot-idle path is deterministic enough
   to gate against under the same NAT-mode wall-clock conditions.

WSL2 networking note: NAT mode (the default) doesn't expose Windows
`127.0.0.1` to WSL. `frida-server.exe` therefore needs
`-l 0.0.0.0:27042` (the auto-start helper passes this), and the WSL
side connects via the host's actual IP or hostname. The user's
`cutestation.soy` works; the default `127.0.0.1` does not. Mirrored
networking would let `127.0.0.1` work both ways — left as a user
preference, not a project requirement.

Known limitations / what Phase B intentionally **doesn't** do:

- **No input injection.** Retail's recorded `trace.jsonl` reflects
  what the engine polled (i.e. live keyboard); the scenario's input
  `trace.jsonl` is unused under `--target retail`. Anything beyond
  the title-idle scene requires a human at the keyboard.
- **No RNG / pause / clock pinning.** Retail's title-idle happens to
  be deterministic across runs (no RNG reads during the idle window);
  scenes that touch RNG would drift. Cross-run stability evaluated
  scene-by-scene as new retail goldens land.
- **State-forcing (save inject + scene jump) is deferred.** Same hook
  surface, separate session per the scope decision in the harness
  roadmap.

## 2026-05-21 — Harness Phase A: input-trace record/replay + scenario runner

Closes the "build-system regression hid between commits" gap that
prompted the harness roadmap two days ago (see "Build-system header
dep tracking" entry below). End-to-end pipeline now lands and locks
in the two scenarios that cover the original failure mode.

Five pieces, three commits:

1. **`src/input_trace.{c,h}` + 20 unit tests (514 total, was 494).**
   Sparse-JSONL parser + writer + lookup. Schema:
   `{"frame":N,"buttons":"0xNNNN"}` — one line per mask change, with
   "the most recent entry's mask holds until the next entry"
   semantics. Strictly-increasing frame order enforced at parse.
   Comments + blank lines tolerated. Pure C; tests cover happy path,
   sparse hold, malformed input, file round-trip, record/replay
   behavior.

2. **`src/main.c` CLI integration.** Five new flags:
   - `--input-trace-record <file>` wraps `tick_cb.input_poll` to
     snapshot `g_input_state[0].buttons` each frame.
   - `--input-trace-replay <file>` replaces `input_poll` with a
     trace lookup, skips `input_init` / DirectInput entirely, pins
     `g_paused=FALSE`, drives a 20 ms virtual clock so the tick
     scheduler always returns TICKED (no Sleep, no DELAYED).
   - `--rng-seed <n>` pins the LCG seed (skipping
     `rng_seed_from_now()`) so title BG scroll + cursor pulse phase
     stay frame-identical across replays.
   - `--max-frames <n>` PostQuitMessage after n rendered frames.
   - `--capture-frames i,j,k` captures ONLY at the listed sim-frame
     indices; filename `frame_<sim_frame>.bmp` so the scenario
     runner can match by number. Legacy `--capture-every-ms`
     untouched when this isn't set.

3. **`tools/scenario-test.py`** Phase A regression harness.
   Discovers `tests/scenarios/<name>/`, runs the exe with the right
   flags, **bit-exact** diffs captured BMPs against `golden/`.
   Mismatches emit a red-tinted overlay PNG so visual review is one
   `Read` away. `--bless` regenerates goldens from a fresh run; that
   path doesn't fail.

4. **`tests/scenarios/boot-idle/`** (3 captures @ 0/30/60, 60-frame
   idle). The trivial baseline — title boots, nothing pressed, cursor
   pulse + BG scroll roll on under the pinned RNG seed.

5. **`tests/scenarios/title-z-press/`** (5 captures @ 0/30/35/44/50).
   Z held for one frame at index 30 → 14-frame select countdown →
   dispatch on frame 44 ("Start a new game" tooltip visible) → main.c
   logs "destination scene not ported yet" + snaps `select_phase=0`
   → frame 50 shows the post-snap pulse. This is the exact failure
   mode of the 2026-05-21 input-bypass bug: a stale `main.o` would
   miss the dispatch entirely, frame 44 would still look like
   frame 30, the diff would land loud.

Pixel-diff strictness decision: **bit-exact**. The smoke validation
showed 3/3 boot-idle frames and 5/5 title-z-press frames bit-exact
across two back-to-back replay runs AND across record-mode vs
replay-mode capture. Mismatch produces a red-tinted overlay PNG;
re-bless after intentional behavior changes. SSIM was rejected
because threshold tuning hides single-pixel offset bugs.

Goldens are gitignored — they're rendered output that embeds vendor
textures (RECETTEAR logo, BG art). `scenario.yaml` + `trace.jsonl`
ship; `golden/` is regenerated locally on first checkout via
`--bless`. See `tests/scenarios/README.md`.

Determinism pins under `--input-trace-replay`:
- RNG seed forced via `--rng-seed`
- DirectInput init skipped (live keypresses can't bleed in)
- WM_ACTIVATE pause pinned off (focus loss can't stall replay)
- Tick scheduler bypassed for a manual 20-ms-per-iter virtual clock

Phase B (Frida hooks on retail exe for ground-truth comparison)
shares this scenario layout — same JSON/PNG schemas — and is the
next session's target if priorities don't shift.

## 2026-05-21 — Build-system header dep tracking (input-bypass regression fix)

The user reported on RDP that arrows + Z had stopped doing anything in
the title menu. Bisecting from the last verified-good commit
(`c2b144c`, title sim port) walked through five known-good intermediate
builds and isolated `d34079e` (settings submenu) as the regression.

Smoke-runs of master here showed a spurious "title: menu item 0
selected" log at boot with no keypress, which led to a per-frame stderr
trace inside `scene_title_sim`: `submenu_state` was reading `-1`
(`0xFFFFFFFF`) by the second sim call even though the init memset had
just set it to 0. A clean `rm -f *.o && make` made the corruption stop
— diagnostic of a stale object file.

Root cause: `src/Makefile` and `tests/Makefile` only declared `%.o:
%.c`, with no header-dep tracking. `d34079e` inserted three fields
(`submenu_state`, `submenu_cursor`, `settings_dirty`) into
`scene_title_anim_t` ahead of `pending_action`. `main.c` was not
touched by that commit, so `make` did not rebuild `main.o`; the stale
object kept writing the action sentinel `-1` to the *old*
`pending_action` offset, which is now occupied by `submenu_state`.
With `submenu_state == -1`, the `scene_title_sim` main-menu input gate
(`cursor_anim == 0 && submenu_state == 0`) failed every frame → arrows
and Z were dead but the dispatch leg fired phantom selections via the
same offset confusion clobbering `select_phase`.

Fix (`520a349`): add `-MMD -MP` to CFLAGS in both Makefiles and
`-include $(DEPS)` so each `.o` declares its real header deps via
generated `.d` files. Touch-test confirms `scene_title.h` →
`main.o`, `music.o`, `sim.o`, `scene_title.o` all rebuild. `*.d`
added to `.gitignore`. 494 tests still pass.

Lesson: every C build for this repo needs header dep tracking from
day one — the cost of `-MMD -MP` is one CFLAGS flag and one
`-include`, and the failure mode (offset corruption on header
extension) is silent. Next time `tests/Makefile` or any new build
unit gets created, copy the pattern.

Follow-up (next two sessions, see `docs/harness-roadmap.md`): set up a
deterministic input-trace harness so this class of regression can't
hide between commits again.

## 2026-05-21 — Title settings submenu producer (FUN_0049a59e state 2)

Ports the bare-path slider producer inside FUN_0049a59e — the title-
screen "Options" submenu that the engine reaches by selecting OPTIONS
on the main menu and pressing A. Lands the input/state-machine half
of the audio-cleanup track's "settings menu slider producer" item.

Result: pressing A on the OPTIONS row of the title menu now (a)
transitions the title sim into submenu state 2 with cursor on row 0,
(b) accepts UP/DOWN/LEFT/RIGHT to navigate the 6-row sliders, (c)
fires SE feedback (0x143 for confirm, 0x146 for cursor/slider tick)
via a new `audio_play_se_by_id` helper, (d) calls
`audio_fade_apply(BGM)` on every BGM-slider change so the running
music re-attenuates immediately, (e) accepts A or B to exit; the
exit handler folds back to main with the cursor seeded on the
OPTIONS row.

- **Module shape:** the producer lives inside `src/scene_title.c`
  (the engine's FUN_0049a59e is the title sim, all submenus
  included). Two static helpers + one new exit-handler call from
  the top of `scene_title_sim`. Non-audio rows 3 & 4 live in a new
  module `src/settings.{c,h}` so other subsystems can read
  text-speed / boolean state without pulling `scene_title.h`.
- **New audio helper:** `audio_play_se_by_id(uint16_t)` in audio.c
  walks the existing 110-entry SE table, finds the slot for the
  resource ID, and delegates to `audio_play_se(slot)`. Pure C, used
  by the title scene to mirror the engine's SE-by-id call sites
  (FUN_00499519). Sibling `audio_se_slot_for_id` exposed for tests.
- **One-shot dispatch fix:** the main-menu select pulse now only
  dispatches on the *first* frame `select_phase` reaches 0xf (was:
  dispatched every subsequent frame, relying on a pending_action
  guard to mask re-publication). The behaviour difference is visible
  for the new OPTIONS branch — without the fix, every frame after
  the select pulse would re-enter the settings submenu.
- **Engine deviations documented** (`docs/findings/title-settings-submenu.md`):
  - Row 2 (SE-B) inc/dec plays SE 0x146 instead of the engine's
    filename-based `re_sys01a_b` SE pair (FUN_0049933c). Filename-
    based SE loading isn't ported yet; cursor SE keeps the user in
    audible feedback.
  - "Clear all data" modal (row 5 + A) is gated but the modal flow
    itself isn't implemented — no save IO to clear. Engine fidelity
    holds: A on row 5 consumes the press + plays SE 0x143 but does
    not exit settings.
  - Save-on-exit (FUN_004905a8) is stubbed; slider state persists
    in the audio_fade module and `settings.{c,h}` for the lifetime
    of the process. Engine saves to `save.dat` + `_save.dat` on the
    exit-dirty path — lands with the save-IO milestone.
- **Tests:** 19 new (513 total, was 494). Coverage: state transitions
  (A on OPTIONS → state=2, exit handler → state=0), 6-row cursor
  wrap mod 6, per-row slider targeting (BGM/SE-A/SE-B/slider3/slider4),
  bounds clamping at both ends, dirty-flag transitions (0→1→2 vs
  0→3), B-also-exits, re-entry clears dirty + cursor, OPTIONS does
  NOT publish to `pending_action`, regression guard that other menu
  items (EXIT etc.) still do.
- **Render deferred:** `FUN_0049c050` (1001 bytes — the settings
  panel renderer) depends on `FUN_0047ca05` (text helper / font
  system). Without the font system the per-row slider values can't
  be drawn. Slated for the font-system milestone — see
  `docs/findings/title-settings-submenu.md` "What's deferred".

Visible verification: smoke boot still clean, exit 0, BGM unchanged.
Settings interactivity verifiable via audio_trace JSONL (paired
`fade_start` + `se_play` lines fire on slider adjust); a manual test
on the user's host where the player navigates to Options will
audibly hear BGM volume drop / restore via LEFT/RIGHT on row 0.

## 2026-05-21 — Audio: per-tick fade animation (FUN_0049966a tail)

Closes item #2 from `audio-backend.md` "Next steps". The volume tail at
LAB_00499a00 walks a two-axis cosine product over `DAT_005d1964`
(=600 by default) frames; this commit ports it end-to-end and wires it
into `music_step`.

- **Pure math** in `src/audio_fade.{c,h}`:
  `audio_fade_progress_centibel(phase, progress, duration, slider)` is
  the two-cos product
  `cos(angle_progress) * cos(angle_slider) * 9600 - 9600` with the
  slider angle reused from the existing per-frame cos arc and a new
  per-progress angle that spans `[0, π/2]`. Defensive clamping on
  slider/progress/duration so call sites stay simple.
- **Hook wrapper**: `audio_fade_apply_progress(channel, phase,
  progress, duration)` mirrors the existing `audio_fade_apply` but
  bypasses the trace emit (the per-tick path can fire up to 600 times
  per fade — a per-frame `fade_start` event would swamp the JSONL).
- **Integration** in `src/music.c::music_step`: replaces the stubbed
  volume-animation tail with the real flow — advance `fade_progress`,
  call `audio_fade_apply_progress` against the BGM channel, set
  `pending_swap_clear = 1` at fade end, reset `fade_progress` on
  phase clear. Engine's `DAT_0438cd70` "carry-over" gate is BSS-zero
  in every observed boot/play trace, so the port pins it to
  "always clear" (annotated; revisit if a future scene flips it).
- **Phase semantics correction**: the music.h comment had
  `1=in, 2=out`. Re-reading the assembly at 0x499a2b (phase==1) vs
  0x499a9e (else) showed the opposite — phase 1's cos(angle_progress)
  starts at 1.0 and decays to 0.0 across `progress`, i.e. audible
  fade-OUT; phase 2 is the inverse. Comment fixed. Setter call
  signature also lines up: `FUN_00499538(duration)` takes a duration
  arg and sets phase 1, `FUN_0049954c()` takes no args and sets
  phase 2 — "here's how long to fade out" + "now fade back in".
- **Tests**: 17 new (475 total, was 458). Pure-math coverage of both
  phases at endpoints + monotonicity, slider/progress clamping,
  degenerate-duration fallback. music_step integration tests run
  short-duration fades to completion under a captured apply hook,
  asserting per-tick centibel direction + final `pending_swap_clear`
  + progress reset.

Smoke boot: title BGM still audible, exe exits 0, no warnings. The
fade tail itself is dormant at boot because nothing yet sets
`pending_fade_phase` — that comes with the title→submenu transition
or the settings-menu producer (item #3 on the queue).

## 2026-05-21 — Audio: `audio_fade_apply` live + revert phase-B deviations

Closes the audio-cleanup track that was queued after SE phase B landed.
Three behaviours converge in one commit:

1. **`audio_fade_apply(channel)` is now real.** Engine call site
   FUN_00499583 is the cos-curve volume mapper that fires before every
   BGM swap and SE play. The math half (`audio_fade_compute`) was
   ported earlier; this commit adds:
   - **Per-channel slider state** in `src/audio_fade.c` for BGM /
     SE-A / SE-B. Defaults 9/9/9 (full volume). The engine's BGM=5
     default in `FUN_004901c2` is a save-data thing — intentionally
     not mirrored until save-load lands; until then, 9 matches the
     audible-volume baseline users already heard. Public setters/
     getters (`audio_fade_set_slider` / `_get_slider`) + a
     `audio_fade_reset` test affordance.
   - **Apply hook** — `audio_fade.c` calls a registered function
     pointer with the computed centibel; the Win32 backend
     (`src/audio.c::audio_fade_apply_hook_win32`) routes to the
     matching AudioPath's `IDirectMusicAudioPath::SetVolume`. The
     indirection keeps `audio_fade.c` test-buildable (no dmusici.h).
   - **Trace event `fade_start`** added to the JSONL schema —
     `{"channel":N,"slider":N,"centibel":N}`. Fires from
     `audio_fade_apply`, so it pairs back-to-back with each
     `bgm_swap`/`se_play` event.

2. **Phase-B engine deviations reverted.** Both wired into the
   audio-fade hookup:
   - `DMUS_SEGF_SECONDARY` → `DMUS_SEGF_QUEUE` in
     `audio_play_se_win32`'s PlaySegmentEx. Engine fidelity. Queueing
     is scoped per-AudioPath, so SE on `path_se_a` doesn't preempt
     BGM on `path_bgm`. The explicit per-trigger Stop right before
     PlaySegmentEx still defeats same-slot re-trigger queueing.
   - Init-time `SetVolume(0, 0)` on both SE paths dropped — the
     per-call `audio_fade_apply(SE_A)` covers it now.

3. **Tests + docs.** 12 new tests (slider get/set, hook capture,
   invalid-channel guards, fade_start trace round-trip). Total 458 in
   the host test suite (was 446). `docs/findings/audio-backend.md`
   updated: status block strips the deviation list, trace schema +
   call-site table updated, Next-steps rewritten (recet.ini → slider
   seeding, per-tick fade animation, settings-menu producer).

Smoke test: title BGM continues to play; `--play-se` fires SEs into
the trace (paired `fade_start`/`se_play` lines per trigger) with
PlaySegmentEx returning S_OK.

**Audible regression on the user's Windows host:** SEs are inaudible
after the revert. BGM is unaffected. Trace events fire normally and
PlaySegmentEx succeeds, so the hooks are wired correctly. The pre-
revert configuration (init-time SetVolume + SECONDARY flag) was
audible on the same host. Treating as an open issue rather than
re-applying the deviation — likely missing a piece of engine init we
haven't ported (FUN_004901c2 save-arena init / recet.ini → slider
seeding / something else). Will surface as we port more of the audio
boot chain.

## 2026-05-21 — Audio: SE backend phase B (live SE playback)

Picks up where the autonomous session left off. Phase A had the
110-entry SE resource table + a trace-only `audio_play_se` shell; phase
B wires the Win32 backend end to end. User-verified audible on the
Windows host: BGM continues uninterrupted while a sequence of SE plays
fires over it.

Four commits land the work:

1. **Mojibake fix** (`main.c`, commit `4740a96`).
   `SetConsoleOutputCP(CP_UTF8)` at WinMain entry. Source files use
   literal `—`/`→`/`⚠` in log strings; the default Windows console
   was decoding them as CP437 (`ΓÇö` etc.) on most hosts. One-line
   fix; no-op for the GUI build (no attached console).

2. **SE table column 2 quirk** (commit `83a3cb5`).
   Reading FUN_00499c63 revealed the +4 column of the 110-entry SE
   table at `&DAT_005d1584` is actually a voice-group / SE-AudioPath
   selector, not "zero padding" as earlier notes claimed. In vendor
   data every +4 cell is zero (verified by re-reading the table from
   the unpacked exe), so path B + the cross-slot voice-stealing scan
   are dead code at runtime — every SE in vendor data routes to path
   A. New engine-quirks #46 documents the dormant routing; the C
   port keeps a single-column resource-ID table since +4 is constant
   zero. `audio.h` schema doc + SE-trigger header comment refreshed.

3. **`--play-se <slots>` harness flag** (`main.c`, commit `e134361`).
   Comma-separated SE slot indices fired post-boot via SetTimer at a
   configurable delay + interval
   (`--play-se-after-ms` / `--play-se-interval-ms`, defaults 1000 / 250).
   Bad indices rejected; cap 16 slots per invocation. Gives phase B
   an in-isolation tester without needing to wire SE calls into the
   title scene's still-unported sim_a body.

4. **SE phase B: live `audio_play_se`** (the main course; this commit).
   - `tools/extract/se-rc.py`: walks `vendor/unpacked/se-extracted/`
     and emits a windres `.rc` with one `<id> WAVE "<abs_path>"`
     entry per WAV (109 entries — slot 2's `0x0135` is in the
     lookup table but absent from `.rsrc`, faithful to engine).
     Gitignored output (`src/se.rc`).
   - `src/Makefile`: regenerates `se.rc` → `se.res.o` via
     `i686-w64-mingw32-windres -O coff -c 65001`; both .exe outputs
     now embed the 3.1 MB SE blob payload (binary grew ~2 MB → ~4 MB).
   - `src/audio.c`: `audio_init` gains 2× `CreateStandardAudioPath`
     for SE-A / SE-B paths (per engine-quirks #46 path B is dead in
     vendor data but the engine creates both, so we do too), plus
     a per-slot `FindResourceA`/`LoadResource`/`LockResource`/
     `IDirectMusicLoader::GetObject(DMUS_OBJ_MEMORY)` loop for the
     110 SE segments. Missing-resource slots silently skip (slot 2
     case). `audio_play_se` gains a Win32 body that mirrors
     `FUN_00499c63`'s bare path: Release prior SegmentState8 →
     explicit Stop → PlaySegmentEx → QueryInterface-upgrade to
     `IDirectMusicSegmentState8` → Release the un-upgraded pointer.
   - Boot log gains the SE preload count:
     `audio: init ok — 21 BGM segments + 109/110 SE segments preloaded`
     `(1 missing/skipped)`.
   - **Two documented engine deviations** (revert when
     `audio_fade_apply` lands — see `audio-backend.md` "Next steps"):
     1. PlaySegmentEx uses `DMUS_SEGF_SECONDARY` (0x8000) instead
        of the engine's `DMUS_SEGF_QUEUE` (0x80). Without SECONDARY,
        primary-segment semantics duck BGM under every SE — Recettear
        doesn't do that. The engine sidesteps via a per-call
        SetVolume that we haven't ported yet.
     2. Explicit `SetVolume(0, 0)` on both SE paths at the tail of
        `audio_init`. Defensive nudge after observing inaudible SE
        on at least one Windows host; the engine never relies on
        path defaults because it SetVolumes per call.

Test count unchanged at 452/452 — phase B's Win32 body is `#ifdef _WIN32`
so the Linux unit suite can't exercise it directly; the trace-shell
tests cover the slot-bounds + JSON escape path that runs unconditionally.

Smoke trace from the final run (`--play-se 0,12,2,69`):
```
{"t_ms":107, "kind":"bgm_swap","track":0,"name":"bgm/retitle2010.wav"}
{"t_ms":1099,"kind":"se_play","slot":0,"name":"se_000_id013d"}
{"t_ms":1714,"kind":"se_play","slot":12,"name":"se_012_id0148"}
{"t_ms":2325,"kind":"se_play","slot":2,"name":"se_002_id0135"}   ← traced; PlaySegmentEx skipped (NULL segment)
{"t_ms":2930,"kind":"se_play","slot":69,"name":"se_069_id029d"}
```

**Carries to next session:** `audio_fade_apply` is the unlock for both
reverting the two engine deviations AND making the per-tick BGM/SE
fade animations real. Need a per-tick fade-counter producer (decay
the path-A/B and BGM fade counters each frame, fire SetVolume against
`audio_fade_compute`). Probably lands next to a wider sim/render
ticker port.

## 2026-05-21 — Autonomous session: 6 audio + harness tasks landed

Worked the queue at `docs/autonomous-session-tasks.md` end to end.
Six commits, ASan/UBSan clean, Win32 build clean, smoke run clean.
Test count 413 → 452.

1. **Per-pixel diff overlay** (`tools/smoke-test.py`). `diff_runs`
   now emits `<run>/diff/frame_NNNNN.png` (new frame with red-tint
   on pixels where any RGB channel diverged ≥ 4) + a tiled
   `<run>/diff-overlay.png` via contact-sheet. Synthetic tests in
   `tools/test_smoke_diff.py` cover self-diff (zero mask),
   hand-modified rect (mask matches exactly), and size-mismatch
   clipping. Mean SSIM self-diff = 1.0000.

2. **MCI debug command recorder** (`src/audio_mci.{c,h}`). Faithful
   port of FUN_00451874 + FUN_00451863. 60×80 buffer at
   `&DAT_06a47aac` (size derived from the dword-zero loop —
   880-byte-per-row arithmetic in earlier notes was wrong; actual
   is 4800 bytes total). 10 new tests including the
   channel-spans-into-next-row engine quirk.

3. **Volume cos-curve fade — math half** (`src/audio_fade.{c,h}`).
   Reverse-engineered: FUN_00503994 is actually a CRT cos() wrapper
   (Ghidra showed it as a 9-byte stub but the disassembly is full
   FPU plumbing). The actual fade math is
   `cos(angle) * (target_centibel + 9600) - 9600`, with frame 0
   short-circuited to hard -10000 (the engine's math curve only
   asymptotes to -9600 — preserved as engine inconsistency).
   `tools/plot/curve.py` + `tools/plot/render_audio_fade_curve.py`
   write `runs/audio-fade-curve.png` (the C tests pin endpoints
   + monotonicity in 1..8 + one hand-computed spot value).
   SetVolume hookup deferred to SE phase 2.

4. **`--audio-trace` JSONL emitter** (`src/audio.{c,h}` +
   `src/main.c` + `tools/smoke-test.py`). Opt-in NDJSON log of
   audio events. Schema:
   `{"t_ms":<u32>,"kind":"bgm_swap"|"se_play","track":<int>|"slot":<int>,"name":<str>}`.
   `audio_trace_json_escape` exposed as a pure-C helper (test
   build doesn't need windows.h). `--audio-trace` flag on the
   smoke harness writes to `runs/.../audio-trace.jsonl`. Verified
   end-to-end against the title-music boot — one line, parses as
   valid JSON.

5. **SE backend phase A** (`src/audio_se_names.{c,h}` +
   `src/audio.c::audio_play_se` + `tools/extract/se-wavs.py`).
   **Major correction**: the autonomous-session brief said "27
   SE entries under RT_RCDATA"; the engine ships **110 entries**
   under a custom *named* resource type `"WAVE"` (string type
   name at `&DAT_005d1ac8`). Two disjoint ID ranges
   (`0x13d..0x182` and `0x29d..0x2c6`) with documented out-of-order
   pairs at slots 2 and 39/40 plus a missing ID at slot 107/108.
   The C table reproduces all the quirks; the extractor walks the
   PE `.rsrc` tree (custom-type-aware) and dumps 109 of 110 WAVs
   (slot 2's id 0x0135 is referenced but absent from `.rsrc` —
   handled by FindResourceA returning NULL). Vendor cross-check
   test re-reads the table from the exe at boot. `audio_play_se`
   is a trace-only shell for now (bounds + se_play emit + return 1);
   **defers** the windres .rc + 2 SE AudioPaths + FUN_00499c63
   live PlaySegmentEx to a follow-up.

6. **Audio-backend doc refresh** (`docs/findings/audio-backend.md`).
   Fade-curve formula + per-frame centibel table, SE resource
   layout (110 entries, custom WAVE type, the two disjoint ranges),
   `--audio-trace` schema + JSON escape rules, constants table
   gained the four fade/SE-type addresses, next-steps list rewritten
   around what actually remains (SE phase 2 dominates).

**Carries to next session:** SE phase B is the unblock for any
in-game SFX. The extractor + table + trace surface are all in place,
so phase B is mechanical Win32 wiring + windres glue. The
`audio_fade_apply` SetVolume hookup naturally lands at the same
time (its second consumer is SE volume blending).

## 2026-05-21 — Harness: auto contact-sheet on smoke runs + ranked roadmap

Small harness commit that lands ahead of the SE-backend port. Two halves:

**Auto contact-sheet.** `tools/smoke-test.py` now invokes the existing
`tools/contact-sheet.py` after a `--capture` run and writes
`runs/<...>/contact.png` (single-source grid) — and additionally
`diff-contact.png` (golden | new, side-by-side rows) when
`--diff-against` is set. Subprocess-shells so behavior matches running
the script by hand. Default cadence unchanged (still 1 fps); the new
PNG is what makes a smoke run multimodally inspectable from inside
assistant conversations. `--no-contact-sheet` for opt-out.

**Roadmap doc.** `docs/harness-roadmap.md` captures the ranked
graphics/audio tooling plan (Tier 1 immediate wins → Tier 3 heavier
work). Notable Tier 3 entry: retail-side Frida instrumentation with
**state-forcing hooks** — inject save / scene state into the
unmodified retail exe so deterministic golden frames can be produced
without an interactive play-through. Cross-link added to `PLAN.md` §6.

**No source code changed.** Existing `--capture` still works exactly as
before; the contact sheet is an additive output. Verified end-to-end
with a back-to-back boot run + self-diff (SSIM 0.9996, both contact
PNGs render cleanly).

## 2026-05-21 — Engine quirk #44 filed (button auto-repeat double-fire)

Retro doc entry into `docs/findings/engine-quirks.md`. Quirk was
already cited in the title-sim port commit (`c2b144c`) and reproduced
by `test_sim_button_ring_repeat_pulses_after_settle`, but the engine-
quirks tour was missing the writeup. Now between #43 and #45 with the
fire/fire/gate/gate/gate steady-state pattern explained, the
unintentionality argument, and refs to `src/sim.c` + the test. The
"(Quirk #44 not yet retro'd)" placeholder note at the top of #45 is
removed.

## 2026-05-21 — DirectMusic 8 audio backend: init + BGM playback (FUN_00498ef4 + FUN_00499200)

Title music is now audible. The selector's stubbed swap-dispatch (from
the sim_b port two commits ago) now drives real `PlaySegmentEx` calls
on a `DMUS_APATH_DYNAMIC_STEREO` audio path. User confirms `bgm/
retitle2010.wav` plays on Windows host via WSLInterop. SE / volume-fade
/ MCI debug bridge still stubbed — next commit.

**What landed:**

- **`src/audio.{c,h}` — DirectMusic 8 backend (BGM-only slice).** Mirrors
  `FUN_00498ef4` (init: CoInitialize → CoCreateInstance Performance →
  InitAudio → CreateStandardAudioPath BGM → CoCreateInstance Loader →
  SetSearchDirectory → preload all 21 BGM segments with SetRepeats +
  Download) and `FUN_00499200` (track-swap: guard duplicate, release
  prior segment-state, PlaySegmentEx with the new segment).
- **21-entry BGM filename table** extracted from `.data` at `0x005d190c`
  via `tools/analyze/pe.py`. Lives in `audio_bgm_filenames[]` as a pure-C
  array so tests can verify track indices.
- **One-shot lookup** — `audio_is_one_shot_track(int)` reproduces the
  engine's `(iVar5 == 0x28 || 0x2c || 0x34 || 0x4c)` guard. Treasure,
  fanfare, clear, staff get `SetRepeats(0)`; everything else gets
  `0xffffffff` (infinite).
- **Music-bridge** — `src/music.{c,h}` exposes a new
  `music_swap_fn_t g_music_swap_fn` pointer. `audio_init` installs
  `audio_play_track_adapter`; `audio_shutdown` clears it. Test builds
  (host gcc, no `_WIN32`) leave the pointer NULL → selector still does
  bookkeeping (`swap_call_count++`, etc.) but doesn't fire a real play.
  This keeps the test build free of `windows.h`/`dmusici.h`.
- **`src/main.c` wiring** — `audio_init(g_hwnd)` slot 17 in the WinMain
  bootstrap (per `docs/findings/winmain-and-bootstrap.md`), right after
  `tables_load_all()` + `scene_title_*_init`. Shutdown call before
  `timeEndPeriod`. Failure is non-fatal (logs to stderr and continues
  muted) — matches the engine's behavior.

**Identified GUIDs (extracted via pe.py + matched against mingw-w64
`dmusici.h`):** see `docs/findings/audio-backend.md` for the full table.
The mingw-w64 `libdxguid.a` exports them natively so the build links
against the standard symbols (no inline GUID definitions needed).

**Tests.** 6 new (total 413, was 407):
- `audio_bgm_table_has_21_entries` — table size + every slot non-NULL +
  every filename has a `bgm/` prefix.
- `audio_bgm_table_well_known_indices` — track 0=retitle2010, 1=town,
  7=over, 11=fanfare, 20=water.
- `audio_bgm_filename_bounds` — bounds-check the indexing helper.
- `audio_one_shot_set_is_exact` — every index `i ∈ [0,21)` correctly
  classified.
- `audio_music_bridge_fires_on_swap` — installing a stub fn into
  `g_music_swap_fn` causes it to fire on a track change (selector ran
  → bridge called with `MUSIC_TRACK_TITLE`).
- `audio_music_bridge_skipped_when_null` — with NULL pointer, selector
  still bookkeeps but doesn't call out.

**Verified at boot:**

```
audio: init ok — 21 BGM segments preloaded
music: swap #1 → track 0 (frame 1)
```

**Not yet ported (next-commit candidates):**

- **SE backend** — port the SE-init loop (27 `RT_RCDATA` resources via
  `FindResourceA` + `loader->GetObject` with `DMUS_OBJ_MEMORY`) + two
  SE AudioPaths + `FUN_00499c63` (per-channel start/stop). Unblocks all
  UI sound cues (cursor move, button click, etc.).
- **Volume animation** — `FUN_00499583` sin-curve fade. Needed for the
  title-screen fade-out band (frames `0x1b6d..0x1ba7`) and in-game fade
  transitions. The selector already computes `g_music.target_volume`;
  the apply call is what's missing.
- **`DMUS_AUDIOF_3D` warning under Wine** — DirectMusic with full audio
  flags can be brittle on some Wine builds (we run on native Windows
  via WSLInterop, so this isn't a current issue, but documenting for
  the Wine port).

## 2026-05-21 — title menu A-press → real EXIT (FUN_0049a59e press-dispatch, item==3)

The smallest scene-transition slice: the EXIT menu item now actually
quits the game. Pressing A on the EXIT line plays the 15-frame select
pulse, then `PostMessageA(hwnd, WM_CLOSE, 0, 0)` fires (the engine's
literal dispatch for `iVar1 == 3` in FUN_0049a59e L524-528), the main
loop's `GetMessageA` returns 0, and shutdown runs cleanly.

This is the first real A-press transition out of the title — every
prior commit landed the player on the title indefinitely.

**What landed:**

- **`scene_title_anim_t.pending_action`** — new outbox field. The
  pure sim sets it to the menu item code (`SCENE_TITLE_MENU_*`) on
  the frame `select_phase` reaches 0xf. Default `SCENE_TITLE_ACTION_NONE
  = -1`. Consumer (main.c) clears it after handling.
- **`scene_title_sim` select-pulse tail rewritten.** Previously
  resetting `select_phase` to 0 at 0xf; now matches the engine —
  pins at 0xf and writes `menu->items[cursor_pos]` into
  `pending_action`. Subsequent frames don't replace the latched
  value (so consumer sees the same action on every poll until
  cleared).
- **`main.c` press-dispatch consumer.** After each
  `tick_step_win32` call, polls `g_scene_title_anim.pending_action`:
    - `SCENE_TITLE_MENU_EXIT` (3) → `PostMessageA(g_hwnd, WM_CLOSE, 0, 0)`,
       leaves `select_phase` at 0xf (window's closing anyway).
    - Anything else → log "menu item N selected — destination scene
      not ported yet" once per item per session, snap `select_phase`
      back to 0 so the player can pick a different item.
- **Engine fidelity for EXIT is bit-for-bit:** same window handle
  (the engine's `DAT_073dfc7c` is our `g_hwnd`), same `WM_CLOSE`
  (0x10) message, same source line in `FUN_0049a59e:526`. The
  engine's `DAT_0964356c = 1` set before the PostMessage is a
  flag we don't need (`scene_title.c` doesn't have any reader of
  it yet; will land if/when we find one).

**Tests.** 4 new (total 407, was 403):
- `test_scene_title_sim_select_phase_pins_at_fifteen` — replaces the
  old "resets at fifteen" test; verifies new pin behavior.
- `test_scene_title_sim_pending_action_default_is_none` — init seed.
- `test_scene_title_sim_pending_action_set_on_select_complete` —
  full pulse cycle latches `pending_action = items[cursor_pos]`.
- `test_scene_title_sim_pending_action_exit_on_exit_item` — move
  cursor to EXIT via DOWN×3, run pulse, assert
  `pending_action == SCENE_TITLE_MENU_EXIT`.
- `test_scene_title_sim_pending_action_set_once_not_replaced` —
  subsequent frames preserve the latch.

**Boot trace unchanged** (same 17-table init, same recet.ini values).
EXIT verification requires synthetic input which the smoke harness
doesn't support yet — covered by the unit tests instead.

**Not yet ported (every other A-press destination):**
- `SCENE_TITLE_MENU_NEW_GAME` (0) / `NEW_HAS_SAVE` (4): engine sets
  `DAT_0964351c = 1` + `DAT_0438bed4 = 1` (loading transition that
  spins for 30 frames then jumps to scene 1 "town"). Needs the town
  scene at minimum.
- `SCENE_TITLE_MENU_OPTIONS` (2): engine sets `DAT_09643524 = 2` +
  `menu_folding_out = 0` (options submenu slides in). Needs the
  options-submenu render branch of FUN_0049c644 + an "options" sub-
  state machine.
- `SCENE_TITLE_MENU_RANKING` (7): engine sets `DAT_09643524 = 3` +
  calls FUN_0049f012(1). Score/ranking persistence not ported.
- `SCENE_TITLE_MENU_CONTINUE_ANY` (1) / `CONT_HAS_SAVE` (5): save-
  bank reader → scene 1 with restored state. Needs save-bank port.
- `SCENE_TITLE_MENU_SURVIVAL` (6) / `HIDDEN_CHAR` (8): even further
  out; both gate on unlock flags we don't simulate.

## 2026-05-21 — sim_b music selector ported (FUN_0049966a)

Second half of the per-frame sim, the music-track selector. Wired into
`tick_cb.sim_b` so the scheduler now drives both halves. No audible
output — the actual DirectMusic backend (FUN_00499200 load+play,
FUN_00499583 volume apply, FUN_00499c63 SE stop) is still stubbed —
but the selector picks the same track index the engine would on every
frame, and a Win32 boot still renders the title pixel-identically to
the prior commit.

**What landed:**

- **`src/music.{c,h}` — full FUN_0049966a port.** Pure-C
  `music_select_track(state, ctx)` returns the desired track index
  (or `-1` keep / `-2` stop sentinel) for any combination of
  `scene_state` + `title_frame_counter` + pause/modal flags +
  forced override. Pure-C `music_step(state, ctx)` runs the whole
  body: SE-stop sweep (110 slots), fade-phase latch, music-speed
  update (0.75 at state 10, 1.0 elsewhere), frame-count advance,
  pause-modal-clear, target-volume curve for the title fade band,
  selector dispatch, and the stubbed swap call.
- **Title-screen specifics:** state 0 + `frame_counter ∈ [0, 0x1b6d)`
  → track 0 (`bgm/retitle2010.wma`) via the `-1 → 0` masking quirk;
  `[0x1b6d, 0x1ba7)` → fade-out volume ramp (1.0 → ~0.90 over 59
  frames, formula `1.0 - (f - 0x1b6c) / 600.0`); `f == 0x1ba7` →
  STOP sentinel (-2); `f > 0x1ba7` → no change.
- **Non-title state branches ported faithfully:** state 7 returns
  NONE (no change); state 9 + `quest_pending != 0` → FANFARE (0xb);
  states 6/8/0xb/0xd/0xe/0xf/0x10 → TOWN (1); pause-modal-override
  (`pause_modal_state != 0 && pause_modal_a == 0 && pause_modal_b
  == 1`) → OVER (7). Stage-dispatch branch (states 1..5, 10, 11, 12
  reading `&DAT_068dd3fc[stage * 0x6cf]`) is stubbed to NONE — lands
  when the stage descriptor table loads.
- **Track-table extracted:** `tools/analyze/pe.py` pulled the 12
  music filenames from .rdata 0x5d1ae4..0x5d1b98 and the 8-entry
  title BGM table at 0x5d1be0 (entries 0/1/3/4/5/6/7/8 — track id
  in low dword, `1` in high dword as some kind of mode flag).
  Stable across rebuilds; embedded in `src/music.c` as constants.
- **Wired into the scheduler.** `tick_cb.sim_b = music_step_default`
  in `src/main.c`. The default wrapper pins scene state to 0 and
  reads `title_frame_counter` / `title_cursor_anim` from
  `g_scene_title_anim`. `title_submenu_state` is hardcoded to 0
  (the press-dispatch branches of FUN_0049a59e that would mutate
  it haven't ported yet — for now the title BGM lookup gate never
  fires and we stay on track 0 forever, matching the engine).

**Stubbed (with comments at each cut-point):**

- The track-swap call (`FUN_00499200`) — would normally load the
  DirectMusic segment from the per-track filename pointer at
  `DAT_09643038[track]` and start playback. Stubbed to just update
  `current_track` and bump `swap_call_count`. When the audio
  backend lands, replace the increment with a real swap.
- The SE-stop call (`FUN_00499c63`) — clears the slot and bumps a
  counter; backend would actually stop the SE channel.
- The volume-animation tail (`FUN_00499583` + `FUN_00451874` MCI
  "VOL %d" command + `DAT_09643108->SetVolume`) — short-circuited
  the same way the engine does it (`DAT_09643108 == 0`).

**Engine quirk #45 — title BGM lookup masks `-1` to `0`.**
`FUN_0049a558` returns `-1` when the cursor-anim+submenu gate fails
(which is always at boot). The caller in `FUN_0049966a` does
`uVar5 = -(uint)(uVar5 != -1) & uVar5`, which masks `-1 → 0`. So
the title screen always plays track 0 (`bgm/retitle2010.wma`)
regardless of language. The table at `0x5d1be0` only gets consulted
once the player opens a submenu (cursor folds fully out,
submenu enters state 4). Documented as
`docs/findings/engine-quirks.md` §45. Faithfully reproduced by
`src/music.c:title_bgm_select` + the `(pick == -1) ? 0 : pick`
conditional in `music_select_track`.

**Tests.** 27 new (total 403, was 376):
- `test_music.c`:
  - `music_init_engine_data_defaults` — initial values match
    .rdata writers (current=-1, forced=-1, duration=0x258, vol=1.0,
    speed=1.0, language=-1, pending_swap_clear=1).
  - 8× selector cases (title bare/fade-band/stop/post-stop,
    submenu-open with valid + invalid language, forced override,
    pause-modal-override on/off, state 7/8/9 quirks, town states
    sweep).
  - 12× step cases (frame-count advance, bare-path dispatches
    track 0 once, state-10 drops speed to 0.75, global pause
    blocks dispatch, SE-stop sweep clears and counts, pending
    fade-phase latches, no-modal clears fade + override, paused_b
    preserves override, target_volume default + fade band ramp,
    stop sentinel dispatches -2, forced override dispatches with
    modal active).

**Visible result.** Boot trace unchanged from the previous commit
(all 17 tables load, recet.ini reads, title BG renders at 1024x768,
exit clean). No audio output yet — backend isn't ported. The
`current_track` global progresses from -1 → 0 → 0 → 0 → … silently;
once the audio backend lands, `bgm/retitle2010.wma` will start
playing on the second sim_b tick.

**Still deferred from the prior commit (this one didn't fix):** the
non-selected menu items still look washed out vs retail — needs an
RE pass on FUN_0049c644's draw block (likely a missing SPECULAR
texture-stage overlay). Unrelated to sim_b.

**Not yet ported (per sim_b's contract):**
- The actual DirectMusic backend (FUN_004902fe init,
  FUN_00498ef4/FUN_00499200 segment load+play, MCI volume bridge).
  Bigger separate concern; needs DirectSound + WMM glue.
- The stage-dispatch branch (states 1..5/10..12 with per-stage
  music ID via `&DAT_068dd3fc[stage * 0x6cf]`). Gated on stage
  scenes porting — the stage descriptor table at `DAT_068dd3fc`
  has stride 0x6cf bytes per entry; data loader not ported.
- The title submenu carrier (`DAT_09643524`). Gated on the
  press-dispatch branches of FUN_0049a59e. Until then, the
  music_step wrapper hardcodes submenu_state = 0.

## 2026-05-21 — Title sim ported (FUN_0049a59e bare path + minimal sim_a)

The title menu now animates: BG scroll keeps going under focus loss
(scheduler drives it now, not the render path), the selected item's
brightness pulses via the slow `pulse_phase` LFO, and UP/DOWN move the
cursor with auto-repeat. Three new pieces:

**What landed:**

- **`src/sim.{c,h}` — minimal sim_a (FUN_004536cb).** Ports the
  button-state ring at the top of the function: per-bit
  current/prev/pressed/held-with-repeat masks for two players (the
  engine's DAT_073dddd0..d6 quad + the 16-short DAT_073dddda repeat
  counter array). Pure-C helper `sim_button_ring_update` exposes the
  per-bit math for tests. Scene dispatch is wired only for state==0
  → `scene_title_sim_default`; the 16 other scene arms (1..16) and
  the four mode-escape sub-blocks (DAT_06a499.. flags) are omitted
  until those scenes port.

  Tail of FUN_004536cb is reduced to `g_sim_frame_count++` — the
  time-dilation float math and the `FUN_004526ab` post-frame helper
  it calls were stubbed (no consumers yet in our skeleton).

- **`scene_title_sim` in `src/scene_title.c` (FUN_0049a59e bare path)**.
  Pure-C; mirrors the path through the function that's actually
  reached at end of `FUN_0049a3a3` ("bootstrap done"), with no scene
  transitions pending and no submenu open (DAT_09643524 stays 0,
  cursor_anim stays clamped at 0 because `menu_folding_out=1`). Runs
  per frame: `cursor_anim` slide (decrement toward 0), `frame_counter`
  advance, A-pressed → `select_phase = 1`, UP/DOWN held with
  auto-repeat → `cursor_pos = (cursor_pos ± 1) mod count`, tail
  `pulse_phase++`. Once `select_phase` reaches 0xf the engine would
  dispatch a scene transition — bare-slice resets it to 0 (no scenes
  to receive control yet, so the player can't actually leave the
  title).

- **`scene_title_anim_t` extended with `menu_folding_out`** (mirrors
  DAT_09643528 — the direction flag for `cursor_anim`). New
  `scene_title_anim_init_fresh` seeds the post-FUN_0049a3a3 state
  (all zero except `menu_folding_out = 1`). Scene-0 state moved from
  static locals in `src/main.c` into `g_scene_title_menu` /
  `g_scene_title_anim` / `g_scene_title_assets_loaded` exports in
  `src/scene_title.c` so sim.c and main.c both reach them by name.
  `main.c::render_dispatch` lost its placeholder `frame_counter++`
  — the sim owns that now.

**Engine quirk #44 — button auto-repeat double-fires across reload.**
The 16-short repeat counter in FUN_004536cb uses two mutually
exclusive `if` branches: `(rep < 1) → rep = 4` (no decrement) and the
`else { rep--; if (rep > 0) clear bit }` gate. So when a held bit's
counter drops to 0, the bit fires on *that* frame (the `> 0` test
fails), AND on the next frame (the reload-to-4 path skips the gate
entirely). Net auto-repeat pattern after the initial 12-frame settle
is fire/fire/gate/gate/gate, period 5 frames. Reproduced exactly;
covered by `test_sim_button_ring_repeat_pulses_after_settle`.

**Tests.** 20 new (total 376):
- `test_sim.c` (8 tests) — button ring rising/held/release/multi-bit,
  the full auto-repeat cycle including the double-fire quirk,
  `sim_init` zeroing, `sim_step_a` frame advance + input piping.
- `test_scene_title_sim.c` (12 tests) — init seeding, idle frame
  advance, pulse-phase ticking under all `cursor_anim` values,
  cursor wrap UP/DOWN, A-press select-pulse start + 15-frame reset,
  input gating during select-pending, A-on-`held`-only is no-op,
  frame-counter past 0x1bc6 ignores input, NULL guards.

**Visible result.** Title screen at 1024×768 looks the same as the
prior commit (positions unchanged), but the selected-menu item's
brightness now visibly oscillates from frame to frame (the engine's
`pulse_phase / 0x2d` slow LFO), and cursor movement responds to the
keyboard/pad bindings. BG vertical scroll continues uninterrupted
when the window loses focus (sim runs from the scheduler at its
fixed 60 Hz cadence, not piggybacking on render).

**Still open from the render commit (this one didn't fix):** the
non-selected menu items look washed out compared to the retail
build — likely a missing texture-stage SPECULAR overlay or per-item
outline pass we haven't found yet. Not gated on the sim port; needs
its own RE pass on FUN_0049c644's draw block.

**Not yet ported:**
- `FUN_0049966a` (sim_b — music track selector). Independent of
  sim_a; lands as its own commit. Scheduler still tolerates a NULL
  `.sim_b`.
- The 16 non-title scene arms of FUN_004536cb (states 1..16) and
  the mode-escape paths (DAT_06a499.. flags). Each is gated on a
  scene that hasn't ported.
- A-press scene transitions out of the title (NEW GAME / OPTIONS /
  RANKING / etc.). Each is gated on its destination scene's port;
  for now the player is parked on the title indefinitely (the 15-
  frame select pulse plays then resets cleanly).
- The intro-movie attract loop (`recet_op.wmv` at frame_counter ==
  0x1be4) — waits on a video player port.

## 2026-05-21 — Title scene wired into main loop (partial FUN_004547ab)

Fifth and final commit of the title-screen port. The render
dispatcher now drives `scene_title_render` on every frame — debug
magenta gone, actual title art on screen. Also includes a critical
correction to `render_quad_add`'s screen-resolution scaling.

**What landed:**

- **`render_dispatch` in `src/main.c`.** Replaces `frame_render_stub`
  as the tick scheduler's `.render` callback. Clears to the engine's
  state-0 ARGB `0xff17f0ff` (pink-blue, visible only at the edges
  before bg2.bmp covers everything), BeginScene, calls
  `scene_title_render`, EndScene, frame-capture sample, Present.
  The full FUN_004547ab dispatch (state 1..16 + device-loss recovery
  + the inner-scene sub-block) lands as those scenes port; for now
  state==0 is the only path.
- **`scene_title_load_assets` + `scene_title_menu_init_fresh` now
  wired** into WinMain after `tables_load_all`. `render_quad_init`
  runs once before that to prefill the static vbuf.
- **Position-scaling bug fixed in `render_quad_add`.** Ghidra's
  decomp of FUN_00404efc hides two FPU multiplications inside its
  `__ftol` artifact calls; the engine actually scales ALL FOUR dst
  components by `screen_w / 640`, not just the width/height. Caught
  by visual comparison against the stock title at 1024×768: with
  positions un-scaled, the menu items + corner element + copyright
  ribbon all sat ~150 px too high. Fix: scale + truncate `dst.x` and
  `dst.y` the same way as `dst.w/h`. One existing test
  (`render_quad_scale_widens_but_not_position`) renamed and updated
  to assert the new, correct scaling. The PROGRESS entry from the
  earlier render-quad commit had the wrong claim — superseded.
- **Animation hack.** `g_title_anim.frame_counter` advances from
  `render_dispatch` so the BG-scroll counter keeps ticking until
  the sim port (FUN_0049a59e) lands and takes over. As a result,
  the BG stops scrolling when the window loses focus
  (`g_paused → WaitMessage → tick scheduler idle`); harmless and
  self-resolves with the sim port.
- **Tests still 356/356.**

**Visible result.** Stock-equivalent title-screen layout at 1024×768:
RECETTEAR logo + scrolling town background + "An Item Shop's Tale"
ribbon + scrolling fuki band + "Start a new game" bubble + NEW
GAME / ITEM ENCYCLOPEDIA / OPTIONS / EXIT menu + EasyGameStation
copyright at the bottom. Positions match the retail build pixel-
for-pixel on the static frame.

**Known visual differences (deferred to sim port):**
- Non-selected menu items render slightly different from stock —
  likely a missing texture-stage SPECULAR overlay or a per-item
  outline pass. Engine's `D3DTSS_COLOROP = D3DTOP_ADD` blend is
  matched, but there may be a second draw pass we haven't found.
- Selected-item brightness is frozen at 0x9f (frame 0 of the
  pulse) — the sin-driven pulse will animate once sim ticks
  `select_phase` / `pulse_phase`.
- Cursor-anim slide (`cursor_anim` counter) is frozen at 0 —
  the menu-fold-in tween needs sim wiring.

## 2026-05-21 — Title scene render ported (FUN_0049c644 — bare path)

Fourth commit of the title-screen path. The actual draw routine —
`scene_title_render` in `src/scene_title.c` — emits the BG, frame
overlay, fuki corner, title01 band, and the menu glyphs (+ selected
highlight) via the new render_quad batcher. Sub-menu sub-screens,
the 7110-frame fade-in overlay, and the trailing UI helpers
(FUN_0043537e/47/17) are intentionally NOT ported here — all gated
on engine counters that stay at BSS-zero until the sim port lands.

**What landed:**

- **`scene_title_anim_t` struct** (`scene_title.h`) — captures the
  5 engine counters the render reads from (frame, cursor pos,
  cursor anim, select pulse phase, slow pulse phase). All five are
  consumed without any wiring; the sim port will advance them.
- **`scene_title_render(dev, menu, anim)`** — direct-line port of
  FUN_0049c644's bare path. Six draw passes:
  1. `bg2.bmp` 640x480 window vertically scrolled by frame counter
     (scroll_y = 360 - frame * 360 / 7140)
  2. `title_waku.tga` full-screen frame overlay
  3. `title_fuki.tga` 416x32 strip at the bottom (corner element)
  4. `title01.tga` 512x256 animated band, x = 64 - cursor_anim * 64
  5. Menu items loop (additive blend, `D3DTSS_COLOROP = ADD`):
     each item is a 160x32 tile (1.0× selected, 0.8× others) from
     fuki.tga at (224, code*32)..(384, (code+1)*32). Selected item
     pulses brightness via two sin()-driven layers (centered on
     0x7f + 0x20 = 0x9f at BSS-zero); non-selected use the engine's
     "1.33123e-43 denormal" trick which we resolve to literal 0x95
     greyscale.
  6. Selected-row decoration (3 tiles: top strip, big cursor glyph
     via the 9-entry LUT at PE 0x005d1cd4, bottom strip).
- **LUT extracted via** `tools/analyze/pe.py bytes 0x005d1cd4 36` —
  `{0,1,2,3,4,0,7,6,5}` mapping menu code → fuki tile. Embedded
  in `title_cursor_glyph_lut[]`.
- **Engine quirks faithfully reproduced.** The selected-item color
  expression `(((v | 0xffffff00) << 8 | v) << 8 | v)` is the engine
  literal — equivalent to `0xFF000000 | v<<16 | v<<8 | v` greyscale.
  Non-selected items get the bit-pattern-as-float trick where
  Ghidra shows `1.33123e-43` for what is really integer 0x95 stuck
  into a float-typed slot to defer the float→int conversion.
- **No call sites yet.** Compiles clean, but `frame_render_stub` in
  `main.c` still emits debug magenta. The next commit wires the
  render-dispatcher and replaces magenta with the actual title.
- **No new tests.** scene_title_render is D3D-bound and tested
  end-to-end via boot smoke once it's wired up. Existing 356 tests
  still pass.

**Deferred (lands with sim port FUN_0049a59e):**
- Sub-menu sub-screens (file-select, options, survival)
- Fade-in overlay (DAT_09643518 > 0x1bc6)
- Final UI helpers FUN_0043537e (sub-cursor), FUN_00435747 (frame
  counter overlay), FUN_00435117 (system-state overlay)
- Animation: the 5 anim counters stay at 0 until the sim ticks
  them; rendered title is static-frame-0 until then.

## 2026-05-21 — Title menu init ported (FUN_0049a324 + FUN_0049a43d)

Third commit of the title-screen path. The engine's menu-items
builder lands as `scene_title_menu_init` in the existing
`src/scene_title.{c,h}` module — pure-C, deterministic, no D3D
dependency. The function takes a save-bank query (4 booleans) and
produces the same 1..8-entry menu the engine generates at
`DAT_09643358..0x09643374`, including the cursor default and the
count-based Y stride / origin (`DAT_005d1bb4` / `DAT_005d1bb8`).

**What landed:**

- **`scene_title_menu_init(save, out)`** — pure-C builder. Encodes
  the engine's nine menu-item codes (named via a new enum, e.g.
  `SCENE_TITLE_MENU_NEW_GAME = 0`, `_RANKING = 7`, etc.), the
  layered "uVar1 = (adv_any ? 1 : 0) | (adv8_any ? 2 : 0)" check
  for which New / Continue variant slots in, and the count-based
  layout switch (counts 6/7/8 each have their own (stride, origin);
  ≤5 hits the default branch).
- **`scene_title_menu_init_fresh(out)`** — convenience wrapper for
  the fresh-boot path (no saves), which is what the wired-up code
  uses until save loading lands.
- **Engine quirk reproduced.** The "hidden character" menu slot
  (item 8, gated on `DAT_056e5788`) is also let in when
  `(uVar1 & 1) != 0` — i.e. when any save bank has Adventure
  cleared. That's because the engine's branch is
  `if ((bVar5) || ((uVar1 & 1) != 0))`, not the conjunction. Port
  matches.
- **Tests.** 6 new menu-builder tests, plus the existing 4 asset-
  table tests:
  - Fresh boot → 4 items `[0, 7, 2, 3]`, cursor 0, stride 33 / origin -16
  - Adv-cleared no adv8 → 6 items `[5, 4, 7, 8, 2, 3]`, stride 33 / origin -30
  - Adv-cleared + populated save → 7 items, cursor 2, stride 30 / origin -36
  - Full unlock (adv1 + adv8 + score) → 8 items, cursor 3, stride 27 / origin -36
  - Hidden-char alone → 5 items including item 8
  - Survival requires `uVar1 == 3` exactly, not just adv8 bit set
  Total: 356 passing (was 350).
- **No call sites yet.** Like commits 1 + 2, this lands the
  building block without wiring. The render-dispatcher port will
  call `scene_title_menu_init_fresh` on first scene-0 enter.
  Direct boot smoke exit=0; magenta unchanged.

## 2026-05-21 — Title scene texture loader ported (FUN_004733d5)

Second commit of the title-screen path. The engine's scene-0
asset-prepare function — called by FUN_004547ab's render dispatcher
the first time it sees `DAT_0438b1c0 == 0` — lands as
`scene_title_load_assets` in a new `src/scene_title.{c,h}` module.

**What landed:**

- **`src/scene_title.{c,h}`.** 7 sprite_t slots and a constant
  asset table (`scene_title_assets[]`) listing `(path, expected_w,
  expected_h)` for each:
  - `bmp/title_bg2.bmp`     1024×1024 — scrolling background panel
  - `bmp/title01.tga`        512×256  — animated band sprite
  - `bmp/title_fuki.tga`     512×1024 — menu glyph atlas
  - `bmp/title_waku.tga`    1024×512  — frame overlay
  - `bmp/pause.tga`         1024×512  — pause-menu submenu (loaded
                                        here, consumed elsewhere)
  - `bmp/result_bord01.tga`  512×256  — result screen
  - `bmp/dungeonbord.tga`   1024×512  — dungeon banner
  Asset paths verified byte-for-byte against PE rdata at
  VA 0x005c8688..0x005c86fc via `tools/analyze/pe.py str`. Texture
  sizes match the literal arguments passed to `FUN_0047193c` in the
  engine; all 7 are powers of two (engine convention).
- **Two-layer split.** Asset table is pure-C and unit-testable on
  Linux. The Win32 layer (`scene_title_load_assets`,
  `scene_title_get`, `scene_title_unload_assets`) wraps `sprite_load`
  and holds the 7 static `sprite_t` slots.
- **No wiring yet.** The render dispatcher port (later commit)
  will call `scene_title_load_assets` on first transition into
  state 0; until then nothing invokes it. Boot smoke direct exit=0,
  magenta clear unchanged.
- **4 new tests** in `tests/test_scene_title.c`: slot count, path
  match against the PE rdata strings, power-of-two sizes, and exact
  (w, h) match against the engine call sites. Total: 350 passing.

## 2026-05-21 — Render-quad primitives ported (FUN_00404efc + FUN_00405354 + FUN_0049b425 + FUN_00404e44)

First commit of the title-screen port path. The engine's 2D draw is
batched: every textured quad is appended to a static 8544-vertex
buffer at VA `&DAT_00605208`, then a single `DrawPrimitiveUP` call
emits all triangles at frame flush. This commit lands the batching
core — pure-C math + Win32 D3D wrappers — with no scene code on top
of it yet. The magenta debug clear is still the only visible output
of the boot smoke; that changes once the title texture loader and
the `FUN_0049c644` render port land in the next two commits.

**Subsystems landed:**

- **`src/render_quad.{c,h}`.** Four engine functions folded into one
  module:
  - `render_quad_add` — FUN_00404efc. Appends one quad (6 vertices,
    2 CCW triangles). `dst[4]` is xywh, `src[4]` is xyxy (engine's
    asymmetric input convention, faithfully reproduced). Width and
    height scale by `(screen_w / 640.0)`; top-left x/y do *not*
    (so UI position stays in 640-relative space while sprite size
    grows at higher resolutions). Top-left is integer-truncated to
    match the engine's `__ftol` pattern. UVs apply the +0.5
    half-texel inset on top/left only, not bottom/right — engine
    quirk, again reproduced.
  - `render_quad_flush` — FUN_00405354. Sets vertex shader to FVF
    `0x1c4` (XYZRHW | DIFFUSE | SPECULAR | TEX1, stride 32) then
    `DrawPrimitiveUP(TRIANGLELIST, count/3, vbuf, 32)` and zeroes
    the counter.
  - `render_quad_state_setup` — FUN_0049b425. Sets the 2D pre-draw
    states: SetVertexShader 0x142 (overridden by flush), FOG off,
    ALPHABLEND on, SRCALPHA / INVSRCALPHA. SRCBLEND/DESTBLEND are
    set *twice* in the original; the dup is reproduced. Texture
    stage 0 gets ALPHAOP/COLOROP=MODULATE + MIN/MAGFILTER=LINEAR.
    Engine relies on D3D8 defaults for COLORARG1/2 and ALPHAARG1/2
    — port does too.
  - `render_quad_init` — FUN_00404e44. One-shot vbuf initializer:
    z=0.0, rhw=1.0, specular=0 on all 8544 vertex slots. Render-
    quad-add never rewrites these fields, just like the engine
    (which leaves bytes 8..15 + 20..23 of each vertex untouched
    after the prefill).
  - `render_quad_bind` — small wrapper around `SetTexture(stage 0)`,
    matches the engine pattern of `dev->SetTexture(0, tex)` calls
    sprinkled between quad-add batches.
- **Two-layer file split.** Top of `render_quad.c`: pure-C math +
  buffer state (compiles on Linux for the ASan test build). Bottom
  (`#ifdef _WIN32`): D3D wrappers. Matches the convention from
  `src/input.c` and `src/tick.c`.
- **Screen-shake hook.** `render_quad_set_offset(ox, oy)` mirrors
  the engine's `DAT_0438cc18 / DAT_0438cc1c` global — added to every
  dst top-left at quad-add time. Untouched in this commit; lands as
  wired-up state when the camera-shake path ports.
- **Bounds check.** Engine has no overflow guard at 8544 vertices —
  a runaway frame would corrupt the global memory immediately past
  `DAT_00647e14`. Port returns 0 from `render_quad_add` once the
  buffer is full so a recoverable failure shows up in tests rather
  than an unrelated crash.
- **Tests.** 10 new pure-C unit tests in `tests/test_render_quad.c`:
  vbuf-prefill spot-checks, 6-vertex emission order (BR / BL / TR /
  BL / TL / TR), UV half-texel-inset asymmetry, screen scaling
  applied to size but not position, screen-shake offset, dst top-
  left integer truncation, buffer-full bounds check, zero-tex-dim
  rejection, reset-keeps-prefill, default-screen-w-zero-means-640.
  Total: 346 passing (was 336).
- **No call sites yet.** `main.c` is unchanged — the new module is
  compiled into `openrecet.exe` (matches `src/Makefile`'s
  `$(wildcard *.c)`) but no boot code calls it. Boot smoke direct
  (`build/openrecet.exe --max-duration-ms 3000` from
  `vendor/original`) exits cleanly with debug magenta unchanged.

**Pre-existing harness flake (unrelated to this commit):**
`tools/smoke-test.py` uses `preexec_fn=os.setsid` which breaks the
`SetTimer → WM_TIMER → DestroyWindow` self-termination path —
reproduced *with the prior input-poll build* (different exe SHA) as
well as the current build. Direct exe invocation terminates cleanly
at `--max-duration-ms`. Track-and-fix later; not blocking. Frames
are still captured and pixel-sampled magenta unchanged, confirming
no functional regression.

## 2026-05-21 — Input poll ported (FUN_0047b73c)

First of the four tick callees now lands real code instead of a NULL
stub. `tick_callbacks.input_poll` is wired to `input_poll` in
`src/input.c`, which mirrors the engine's keyboard + multi-joystick
DInput poll, decodes raw button state, and OR's it through the
recet.ini binding table into `g_input_state[0].buttons` each frame.

**What landed:**

- **`src/input.c` — three new pure-C decoders.** `input_joystick_decode`
  fans a `DIJOYSTATE2`-like input into a 20-bit "pressed" array
  (4 D-pad bits OR'd from POV-hat + stick axes, 16 buttons). POV is
  the standard DInput angle-times-100 encoding with explicit cases
  for all 8 cardinals and diagonals; centered (-1 / 0xFFFFFFFF) gives
  zero. Stick dead-zone is fixed ±500 on `lX`/`lY` (range was set to
  ±1000 in init, so 50% deflection). `input_apply_joystick_block`
  matches binding values against a per-joystick virtual-button range
  (`0x27 + joy_idx * 0x14`) and OR's the slot's bit into the output
  mask; `input_apply_keyboard_block` does the equivalent via the
  41-byte DIK lookup at `0x005cbc2f`.
- **`src/input.c:input_dik_table[40]` + `input_binding_mask[14]`.**
  Bytes extracted via `tools/analyze/pe.py bytes 0x005cbc2f 41`.
  Binding-slot bit layout (UP=0x04, RIGHT=0x01, DOWN=0x08, LEFT=0x02,
  A=0x10..E=0x100, skill0..4=0x200..0x2000) matches downstream
  readers — verified the camera-cursor code at lines 50410-50420 of
  `all.c` reads exactly these bits.
- **`src/input.c:input_bindings_load`.** Flattens
  `recet_ini.pad[2][9]` + `skill[2][5]` into the engine's
  interleaved per-controller layout (`pad[N][0..8]` then
  `skill[N][0..4]`, 14 shorts per controller block). 4 blocks total —
  blocks 2..3 stay zero (the engine's outer joystick loop reads BSS
  past the 2-controller end; see quirk #41).
- **`src/input.c:input_poll`.** Win32 wrapper that queries each
  acquired DI device, decodes raw state via the helpers above, and
  walks the 4 (joystick) / 2 (keyboard) binding blocks. Pre-clears
  the button accumulator at poll start — at the default
  `speed=0 / 60FPS` path this is bit-identical to the engine's
  "clear after render" pattern; at higher speeds the engine
  accumulates multiple polls per render and we don't. Revisit when
  the FUN_004547ab render port lands a post-render clear hook.
- **Init-side fix.** Switched the joystick `SetDataFormat` from
  `c_dfDIJoystick` (80 bytes) to `c_dfDIJoystick2` (272 bytes) to
  match the engine's custom DIDATAFORMAT at `0x0051c4cc`
  (`dwDataSize = 0x110`). The 80-byte format would have made
  `GetDeviceState(sizeof(DIJOYSTATE2), &st)` fail with
  `DIERR_INVALIDPARAM` — the previous boot smoke didn't hit this
  because nobody was calling GetDeviceState yet.
- **`src/main.c`.** Wires `input_bindings_load(&g_ini)` after
  `input_init`, and replaces the NULL `tick_cb.input_poll` with the
  real `input_poll` function. 4 engine quirks documented (#40-43).
- **Tests.** 20 new tests in `tests/test_input_poll.c` cover POV-hat
  all 8 directions, stick dead-zone, button-high-bit-only decoding,
  binding application with per-joystick virtual base, keyboard DIK
  mapping (with default vendor bindings), and the recet.ini
  flattening round-trip. Total: 336 passing (was 316).
- **Smoke boot.** `tools/smoke-test.py --scenario boot --duration 4
  --capture`: exit=0, 4 frames captured, all solid debug magenta —
  unchanged from the pre-input-poll baseline.

**Engine quirks documented (#40-43):**
- #40: both controllers' bindings funnel into player-0's single
  output slot (`(local_8 / 2) * 0x2a` integer divide).
- #41: joystick scan iterates 4 outer binding blocks but only 2 are
  populated; blocks 2..3 read BSS zero bindings and never match.
- #42: Poll-failure retry loop checks Acquire's return against
  `DIERR_NOTACQUIRED`, a code Acquire never produces; effectively a
  single-iteration loop.
- #43: each joystick is `Poll()`'d four times per frame (once per
  binding block); port collapses to one Poll + per-block apply for
  the same bit-for-bit output.

**Deferred until the next big port:**
- Post-render input clear with multi-poll accumulation semantics —
  needs `tick.c` to grow a callback hook; lands with `FUN_004547ab`
  (frame render).
- Sim halves `FUN_004536cb` / `FUN_0049966a` — they're the first
  readers of `g_input_state[0].buttons` and will exercise this
  port end-to-end.

## 2026-05-21 — Game-tick scheduler ported (FUN_0047be92 + FUN_0047be2f)

Heart of the engine's main loop is now driven by our own code instead
of the magenta-clear placeholder. The scheduler dispatches at the
configured fixed-timestep frame rate (60 FPS by default, selectable
via the speed table at `0x005cbc58`); the four callees it hands off to
— input poll, two sim halves, frame render — are stubbed for now and
land one-per-commit.

**Subsystems landed:**

- **`src/tick.{c,h}` — FUN_0047be92 + FUN_0047be2f.**
  - `tick_step_with_now(now_ms, has_device, &callbacks, &out_sleep_ms)`
    is the pure-C dispatcher, taking the four big callees as function
    pointers so the scheduler can stand alone and tests can mock them
    under ASan. All arithmetic in 1/3 ms units (matching the engine's
    `*3` + `% threshold` residue pattern), so sub-ms frame budgets
    work without floating point.
  - `tick_step_win32(has_device, &callbacks)` is the Win32 wrapper that
    bundles QPC + Sleep on top.
  - `tick_now_ms()` mirrors FUN_0047be2f: `QPC.QuadPart * 1000ull /
    QPF.QuadPart` truncated to uint32, with `timeGetTime()` fallback
    when either QPC value reads zero.
  - Speed-threshold table `g_tick_speed_thresholds[5]` extracted via
    `tools/analyze/pe.py bytes 0x005cbc58 32` and verified
    byte-for-byte against the engine.
  - All scheduler globals (`now_ms`, `prev_ms`, `delta_thirds`,
    `leftover_thirds`, `speed`, `pending_speed`, `state`, `state_alt`,
    `state_seed`, `frame_count`, `flag_dddd0`, `flag_dddfa`) live in
    a `g_tick` struct with named members matching the engine's
    DAT_073de618.. / DAT_073dfca4.. / DAT_0438ccd8.. globals.
- **`src/main.c` — main loop now drives the scheduler.** Replaced the
  `tick_and_present()` placeholder call with
  `tick_step_win32(g_d3d && g_dev, &tick_cb)`. The old debug-magenta
  clear/draw/capture/present body now lives in `frame_render_stub`,
  which is passed as the `render` callback — same visible behaviour,
  but now exercised through the real dispatcher. The other three
  callbacks (`input_poll`/`sim_a`/`sim_b`) are NULL until their ports
  land — the scheduler tolerates NULL callbacks.

**Behavioral validation:**

- 316 unit tests pass under ASan/UBSan (was 298). 18 new tests for
  `tick.c`:
  - Speed-threshold table bytes vs `.rdata` dump.
  - First-frame huge-delta normalisation (prev=0 → one tick + leftover=0).
  - Sim-loop count vs latched speed (`speed=0` → 1 sim, `speed=4` → 5).
  - Adaptive-sleep band (delta=29..40 in 1/3 ms steps; sleep_ms = 5, 4,
    1, 1, 0=busy-spin at the boundary).
  - Steady-state 60 FPS residue accumulation (delta=51 each frame with
    threshold=50 carries 1, 2, 3, … in `leftover_thirds`).
  - Input poll firing at ≥1/60 s delta but NOT when delta is smaller.
  - State machine: state=1 skips sim/render (but commits leftover/prev),
    state=2 transitions to 1 after one tick, state_alt mirrors state_seed.
  - `has_device=0` early-return after sim, before render (engine order).
  - Per-frame flags clear on tick, persist on delayed pass.
  - Pending-speed latches at the top of the next frame, not mid-frame.
  - NULL callbacks are safe (shell-port scaffolding).
- Boot smoke (`./tools/smoke-test.py --target openrecet --scenario boot
  --duration 3 --capture`): `exit=0, 3 frames`. Captured frames are
  solid debug magenta (160,32,96) at 1024×768 — visually identical to
  the pre-scheduler boot, just driven by `tick_step_win32` now.

**Engine quirks documented:**

- **Speed-threshold lookup is OOB-unsafe.** `(&DAT_005cbc58)[DAT_0438ccd8]`
  has no bounds check; the engine relies on the unmapped F-key handler
  only ever writing values in `[0..4]`. Test for `speed = -1`
  intentionally skipped — would force ASan to read OOB into adjacent
  globals.
- **Dead clamp in adaptive sleep.** Inside `if (remaining < 0xb)` the
  engine has `if (0x1e < remaining) remaining = 0x1e;` — unreachable
  given the outer guard (remaining is already < 11). Preserved as a
  comment in `src/tick.c`; harmless leftover from an earlier formula.
- **`state_alt = state_seed` is a no-op at boot.** Both globals are
  BSS-zero, so the per-frame copy doesn't do anything in practice. We
  preserve the write for byte-identical behaviour once whichever code
  writes `state_seed` lands.

**Deliberate divergences:**

- The four big callees (FUN_0047b73c input poll, FUN_004536cb /
  FUN_0049966a sim halves, FUN_004547ab frame render) are NULL stubs
  in this commit. The render callback is filled in by
  `frame_render_stub` (the old magenta-clear path) to preserve the
  visual smoke-test signal until FUN_004547ab lands.
- Pure-C scheduler entry takes callbacks as function pointers, where
  the engine has direct calls. Necessary for ASan-clean testing and
  to keep `tick.c` decoupled from the four big functions; once they
  all land we could fold them into direct calls again, but there's no
  real upside.
- Engine writes to `DAT_0438ccd8` and `DAT_0438ccdc` from an unmapped
  F-key handler. Our `g_tick.pending_speed` stays 0 until that
  handler lands — meaning we always run at the 60 FPS target.

**Not in this commit (deferred):**

- `FUN_0047b73c` — input poll. 325 lines of keyboard + joystick state
  read with POV-hat angle decoding (centidegree values 4500/9000/
  13500/18000/22500/27000/31500 → direction bits). Next.
- `FUN_004536cb` / `FUN_0049966a` — the two sim halves. 322 / 267
  lines respectively. Will read decomp before scoping.
- `FUN_004547ab` — frame render. 303 lines. Replaces the magenta-clear
  stub with the engine's real Clear+BeginScene+...+Present sequence;
  likely drives the 24 render-layer objects already initialised in
  `src/layers.c`.

**Files:**

- new `src/tick.{c,h}`, `tests/test_tick.c`
- updated `src/main.c` (include tick.h, replace tick_and_present with
  tick_step_win32 + rename old body to frame_render_stub),
  `tests/Makefile`, `tests/test_main.c`,
  `docs/findings/winmain-and-bootstrap.md` (new §"Game tick scheduler"
  + main-loop annotation + open-subsystems table refresh)

## 2026-05-21 — Pre-window block closed: RNG + math3d + FUN_00451790

Closes the last three open steps in the WinMain pre-window chain (steps
2, 3, and 5 from `docs/findings/winmain-and-bootstrap.md`). After this
commit, every call between `timeBeginPeriod` and `create_main_window` is
either ported or documented as a deliberate no-op.

**Subsystems landed:**

- **`src/rng.{c,h}` — engine LCG + time-to-seed.** Reimplements
  FUN_005041f6 (`x = x * 0x343fd + 0x269ec3; return (x >> 16) & 0x7fff`),
  FUN_00471089 (`rand / 32768.0` unit float), FUN_0050bcff (time → seed
  scalar with tzset-style constants pulled from `DAT_006038d0`: TZ
  offset 28800s, DST bias -3600s, epoch literal 0x7c558180), and a Win32
  wrapper for FUN_005045eb that bundles `GetLocalTime` +
  `GetTimeZoneInformation` → DST flag → seed write. The engine's RNG
  constants are bit-identical to MSVC's `rand()` so the first values
  from seed=1 are the canonical 41 / 18467 / 6334 / 26500 / 19169
  sequence — covered by a unit test (one of those compiler-fingerprint
  facts that's nice to have pinned).
- **`src/math3d.{c,h}` — vec3/mat4 helpers.** Portable C
  implementations of `vec3_normalize` (FUN_004a1f67),
  `mat4_lookat_rh` (FUN_004a3b52), `mat4_perspective_fov_rh`
  (FUN_004a3ee8), and `mat4_mul` with internal-temp aliasing support
  (thunk_FUN_004a2a03 = D3DXMatrixMultiply). The engine reaches D3DX
  through `FUN_004cdd9f`'s indirect-dispatch table (x87 / MMX / SSE
  backends selected at boot); we use a single portable implementation
  since algebraic equivalence is what matters at this layer.
- **`src/prewindow.{c,h}` — FUN_00451790 (WinMain step 2).** Writes the
  six named globals: `flag_b1c4=0, flag_b8cc=0, camera=(10,61,-203),
  flag_b1c0=1, flag_bf84=0, flag_bf88=0`. Then runs FUN_00404e44
  (8544-entry object table — each 32-byte entry gets field0=0, y=1.0,
  field12=0 written; other 5 dwords stay BSS-zero) and FUN_00452569
  (100 randomized particles, 6 rand calls + alive=1 per particle =
  600 LCG steps total). Finally constructs the boot view+projection
  matrices: lookat with degenerate eye=target=(0,0,0) (`DAT_06a47110`
  is BSS-zero at this point in WinMain — see the engine quirk note
  below) and perspective with fov=π/4, aspect=4/3, near=10, far=2000.
- **`src/main.c` — wiring.** Pre-window block now reads:

  ```c
  timeGetDevCaps + timeBeginPeriod(min);
  prewindow_init();      // step 2 — particle table from seed=1
  rng_seed_from_now();   // step 3 — reseed for game-tick randomness
  timeBeginPeriod(10);
  // step 4: recet.ini path build (already done)
  // step 5: FUN_0047aa30 — empty stub (intentionally omitted)
  // step 6: log no-op
  recet_ini_load(...);   // step 7
  create_main_window();  // step 8
  ```

**Behavioral validation:**

- 298 unit tests pass under ASan/UBSan (was 271). New tests:
  - 9× rng — sequence vs MSVC, time-seed determinism, year-range
    rejects ([0x46, 0x8a] = 1970..2038), leap-year bumps (Feb→Mar +86400s).
  - 9× math3d — lookat translation correctness, perspective field map,
    matmul with output aliasing.
  - 9× prewindow — named globals, object table first/last + zeros at
    untouched fields, particle alive flags, value-range checks
    (pos.x/y ∈ (-5,5), pos.z ∈ (-17.5,-12.5), rot ∈ ±π/20), particle 0
    bit-exact against hand-computed seed-1 reference, post-init RNG
    state matches 600 manual LCG steps, proj-matrix field values, view
    contains NaN/inf (degenerate-input documentation).
- Boot smoke: exit=0, all 17 tables load, recet.ini loads, window
  1024×768. No visible regression on the magenta-clear+sprite tick.

**Engine quirks documented (and faithfully reproduced):**

- **Particle randomization runs before the time-based reseed.** WinMain
  step 2 (FUN_00451790) consumes the RNG with its `.data` initial value
  of 1 *before* step 3 (FUN_005045eb) replaces the seed. So the 100
  particles end up identical every boot — same sub-pixel jitter on
  whatever effect ends up consuming them. Almost certainly deliberate:
  developers wanted the deterministic boot scene without wiring a
  separate RNG.
- **Lookat eye position `&DAT_06a47110` is in the BSS-uninitialised
  region of .data.** Raw size in the unpacked binary (0xdbe00) doesn't
  cover that VA — so the vector reads as (0, 0, 0) when FUN_00451790
  runs. Combined with target=(0,0,0) that makes the lookat
  mathematically degenerate (zaxis tries to normalise (0,0,0) →
  divide-by-zero → NaN/inf). Engine produces a garbage view matrix at
  this point and never reads it — a later in-game camera setup
  overwrites it before any vertex transform consumes it. Port
  reproduces the call as-is; one prewindow test pins the
  NaN-or-inf-somewhere expectation so a future "let's clean up the
  garbage matrix" refactor would have to deliberately stomp on it.
- **FUN_0047aa30 is a 1-byte empty stub.** Vestigial leftover from a
  removed log call between init phases (FUN_0047aa31 is similarly
  empty — the one documented as the release-build logger). Port
  intentionally omits the call.

**Deliberate divergences:**

- The engine's `FUN_005045eb` caches the last-checked UTC year/month/
  day/hour/minute and skips `GetTimeZoneInformation` when unchanged
  — an optimisation that mattered when GTZI was slow. Port doesn't
  cache: it's called once per boot.
- The engine's matmul dispatcher (`FUN_004cdd9f`) picks between x87,
  MMX, and SSE backends at startup. Port uses a single portable C
  implementation; bit-exact match with the engine's per-CPU path
  isn't pursued (the engine itself drifts across CPUs).
- `mat4_mul` adds an internal temporary so `mat4_mul(view, view, proj)`
  works. D3DXMatrixMultiply in the official D3DX runtime does the same;
  the engine's per-CPU paths may or may not. Safer to do it
  unconditionally.

**Not in this commit (deferred):**

- Consumers of the camera globals (`DAT_0438cd64..6c`) and the
  particle table. We've found the initialiser but no reader of the
  particle pos/rot/alive arrays yet — they likely feed an as-yet-
  unported render path (title screen effect? loading-screen flair?).
  When that reader lands, it will reuse `g_prewindow.particle_*`
  directly and rename the field accessors at that point.
- Consumers of the 8544-entry object table at `DAT_00605214`. Same
  story — initialiser-only port; `struct prewindow_object` has named
  fields for the three writes but the other 5 dwords stay as `pad08`
  / `pad16_28` until we find a real consumer.

**Files:**

- new `src/rng.{c,h}`, `src/math3d.{c,h}`, `src/prewindow.{c,h}`
- new `tests/test_rng.c`, `tests/test_math3d.c`, `tests/test_prewindow.c`
- updated `src/main.c` (call order before create_main_window),
  `tests/Makefile`, `tests/test_main.c`
- updated `docs/findings/winmain-and-bootstrap.md` (steps 2/3/5 closed)

## 2026-05-21 — `recet.ini` reader ported (FUN_0047a474, pre-window init)

**Subsystems landed:**
- `src/recet_ini.{c,h}` — pure-C parser for FUN_0047a474
  (`docs/decompiled/by-address/47a474.c`). Handles 33 keys: a
  2×9 pad grid + 2×5 skill grid under `[option]` (formatted-key
  match on `padNM`/`skillNM`), 22 `[setup]` scalars (`winmode`,
  `screen`, `fps`, `windowpos`, etc.), 1 `[debug]` key (`camfree`),
  and 2 `[config]` keys (`se`/`mu`, clamped to `[0,9]`).
  Pre-baked defaults match the byte tables at `0x005c81d8` (pad)
  and `0x005c8204` (skill) in the unpacked binary, with the engine's
  `+1` adjustment baked in.
- Win32 entrypoints `recet_ini_default_path()` (mirrors the engine's
  `_splitpath(argv[0]) + wsprintfA "%s%s/recet.ini"` dance via
  `GetModuleFileNameA` + tail-strip) and `recet_ini_load()` (fopen+
  fread+parse). Parser stays pure-C so ASan tests run on Linux.
- `src/main.c` — `recet.ini` now loaded in `WinMain` **before**
  `create_main_window` (matching engine step 7 in `winmain-and-
  bootstrap.md`). `g_windowed` and the window's initial RECT now
  come from `g_ini.winmode` / `g_ini.width` / `g_ini.height`;
  same with the D3D `BackBufferWidth`/`Height`. Boot trace logs
  `recet.ini: winmode=1 screen=2 (1024x768) se=9 mu=9` against the
  vendor file.
- `tests/test_recet_ini.c` — 14 unit tests covering: empty-input
  defaults, default pad/skill tables byte-for-byte, all four
  `screen`→(w,h) branches incl. fallthrough, every `[setup]`
  scalar in engine order, `[option]` grid override, case-insensitive
  section+key match, `;`/`#` comments + blank lines, whitespace
  around `=`, **bgnodisp auto-derives from easydisp (quirk #37)**,
  se/mu clamp [0,9] (over + under), unknown keys/sections ignored,
  no-trailing-newline parse, vendor recet.ini round-trip.

**Behavioral validation:**
- 271 unit tests pass under ASan/UBSan (was 257).
- Boot smoke (`./tools/smoke-test.py --target openrecet --scenario boot
  --duration 3 --capture`): `exit=0, 3 frames`. Window now opens at
  1024×768 instead of the hardcoded 800×600 — matches what the
  original Recettear opens at on this user's machine.
- Path resolution: CWD-first (matches our dev convention of
  `cd vendor/original` before invoking the exe), falls back to
  next-to-exe via `GetModuleFileNameA` for the eventual deployment
  shape where `openrecet.exe` lives alongside the data files.

**Engine quirks documented (and faithfully reproduced):**
- **`bgnodisp` is dead text — overwritten from `easydisp` (#37).**
  Vendor `recet.ini` carries `bgnodisp=0` under `[setup]` but the
  loader doesn't read it; instead, after the main read loop,
  `DAT_0438b18c = DAT_0438b19c` unconditionally aliases the field to
  `easydisp`. Any explicit value in the ini is dropped.
- **`[debug] camfree` is read twice with the same section+key (#38).**
  Two adjacent `GetPrivateProfileIntA` calls write to the same
  global; second value sticks but both calls hit the same ini
  entry. Dead duplicate code from a refactor. Port reads once.
- **Three more keys never read anywhere in the binary (#39).**
  `pfnouse`/`fontmode1`/`fontmode2` ship in vendor `recet.ini` but
  no `GetPrivateProfile*` call touches them. Likely vestigial from
  earlier engine revisions. Port silently ignores (matches Win32
  semantics for missing keys).

**Deliberate divergences:**
- Path resolution adds a CWD-relative `recet.ini` lookup before the
  engine's next-to-exe path build. Required for our dev workflow
  (exe in `build/`, data in `vendor/original/`); behaviour identical
  for a deployment where the exe sits alongside its data.
- `recet_ini_parse` uses an in-process INI tokenizer instead of
  per-key `GetPrivateProfileIntA` calls — same semantics for every
  key in the engine's read set (case-insensitive lookups, `atoi`
  parsing, defaults on missing key). The only edge case we don't
  match is Win32's `0x` / `0` → hex/octal prefix handling; not used
  anywhere in vendor `recet.ini`.

**Not in this commit (deferred):**
- **`FUN_0047a804` shutdown save-back** (`[config] se`/`mu` always,
  `[setup] winx`/`winy` when `windowpos != 0`). Belongs to the
  shutdown chain — lands when that whole chain is ported.
- **Consumption of `pad[]`/`skill[]`** by the input subsystem. The
  values are loaded into `g_ini` but `src/input.c` currently only
  initialises DInput devices; wiring lands with the input-poll port.
- **`FUN_00451790`** (early camera/particle math init, step 2 of
  WinMain). Sized small in decomp (36L) but pulls in
  lookat/perspective/matmul/normalize/RNG helpers — deferred to a
  later milestone where those helpers earn their keep with real
  rendering.

**Files:**
- new `src/recet_ini.{c,h}`, `tests/test_recet_ini.c`
- updated `src/main.c` (load + wire into window/D3D init order),
  `src/Makefile` (picks up `*.c` automatically — no edit needed),
  `tests/Makefile`, `tests/test_main.c`
- new `docs/formats/recet-ini.md`
- new entries (#37, #38, #39) in `docs/findings/engine-quirks.md`
- updated `docs/findings/winmain-and-bootstrap.md` step 7

## 2026-05-21 — Phase B [+1]: `idx/stage.idx` parser

**Subsystems landed:**
- `src/tables_stage.{c,h}` — pure-C parser for FUN_00475270
  block #1 (`docs/decompiled/by-address/475270.c` L55..L329 +
  L3174..L3957 — the largest table parser in the loader by far,
  ~1000 lines of decomp). Defines the 21-record stage table
  (`stage:0-1`..`0-5` + `stage:1-1`..`1-16`) at base `&DAT_068dd2f8`,
  stride 0x1b3c = 6972 bytes. `_Static_assert` guards on 24 critical
  field offsets + total record size.
- `src/tables.c` — replaced the stage.idx stub with the real
  loader. No new resolver wiring (stage.idx is self-contained —
  no cross-table refs). Boot trace logs `(stages=S maps=M
  mapcameras=MC sunpos=S1 sunset=S2 moonpos=MP)`.
- `tests/test_tables_stage.c` — 28 unit tests covering: byte-offset
  layout, empty input, comments/blanks, lines-before-header
  dropped, all 21 stage-ID dispatch entries (both 3-byte and
  4-byte forms), unknown-ID fallback (quirk #34), every shape
  class (int / int→float / float / flag / string / slot string /
  int×3 / float×3 / float×2-colon / float×2-space), sunpos numeric,
  sunpos:off short-circuit, sunset numeric, **sunset:off broken
  (quirk #36)**, **moonpos shared coords (quirk #35)**, multi-record
  threading, no-trailing-newline EOF, map[] slot overflow safety,
  mapcamera[] threading, and a vendor-shape miniature integration
  smoke.

**Field key inventory:** 57 fully-dispatched keys covering map
geometry, camera, lighting (directional + ambient + maplight pairs),
water surfaces, weather flags, fog/colour ramp, and misc. ints. All
documented with their byte offset, type, default value, and source
key in `docs/formats/data-text.md`.

**Behavioral validation:**
- 257 unit tests pass under ASan/UBSan (was 229).
- Boot smoke: `idx/stage.idx — 22434 bytes (stages=20 maps=219
  mapcameras=0 sunpos=15 sunset=0 moonpos=0)`. Cross-checked:
  vendor file has exactly 20 `stage:` headers, 219 uncommented
  `map:` lines (`/map:...` comment lines correctly skipped), 15
  `sunpos:N:N:N` numeric lines, 5 `sunpos:off` short-circuits
  (mode=0, not counted in the sunpos= tally), 0 `sunset:` or
  `moonpos:` lines.

**Engine quirks documented (and faithfully reproduced):**
- **Unknown stage IDs alias to `1-16` (#34).** The chain-default
  `uVar5 = 0x14` collides with the last entry's index, so a
  typo'd `stage:foo` opens a record indistinguishable from a
  real `stage:1-16` on read-back. Dormant in vendor.
- **`moonpos:` shares X/Y/Z storage with `sunpos:`/`sunset:` but
  not the mode flag (#35).** Only `sunpos:`/`sunset:` touch
  `sunpos_mode`; `moonpos:` sets a separate `moonpos_set` flag and
  overwrites the sun coords. A record with both sunpos and moonpos
  ends up with sunpos's mode and moonpos's coords. Dormant in vendor.
- **`sunset:off` is broken (#36).** The "off" short-circuit
  compares against the literal string `"sunpos:off"` (the binary
  has two interned copies of `"sunpos:off"` at 0x005cab4c and
  0x005cab80 — but no `"sunset:off"` anywhere), so a real
  `sunset:off` line falls through to the numeric path. Dormant
  in vendor.

**Safety divergences (documented, not present in engine):**
- `map:` slot cap (engine bumps count unconditionally; port stops
  writing past slot 19 to avoid clobbering the minimap field).
- `mapcamera:` slot cap (engine bumps count unconditionally; port
  stops writing past slot 1 to avoid clobbering mapcamera_count).
- Post-loop unrelated globals (13 writes to `_DAT_0438cc6c..`) are
  player-inventory defaults, not stage state — deferred to the
  gameplay-state init port.

**Files:**
- new `src/tables_stage.{c,h}`, `tests/test_tables_stage.c`
- updated `src/tables.c`, `tests/Makefile`, `tests/test_main.c`
- new docs section in `docs/formats/data-text.md`
- new entries (#34, #35, #36) in `docs/findings/engine-quirks.md`

**Phase B fully complete.** All 15 of the originally-tracked Phase B
files (14 named + the tuto loop counted as 1) had parsers landed
in the 2026-05-20 sweep; this commit closes out the remaining
`stage.idx` stub — file 0 of the engine's load order, deferred at
the time because of its size. The full loader chain is now end-
to-end real: no stubs remain in `tables.c`.

## 2026-05-20 — Phase B [15/15]: `data/enemylist.txt` parser

**Subsystems landed:**
- `src/tables_enemylist.{c,h}` — pure-C parser for FUN_00475270
  block #14 (`docs/decompiled/by-address/475270.c` L2581..L2899).
  Two engine globals populated: a 10×60 grid of 752-byte
  `enemylist_section_t` at `&DAT_0053f8e8` (451200 bytes), and a
  10-dword wisp drop table at `&DAT_073d8630`. Each section carries
  `floor_lo`/`floor_hi` + 31 enemy slots (`{enemy_id, variant,
  count}`) + 31 drop slots (`int32_t item_id[3]`). `_Static_assert`
  guards on all four major byte offsets + the total 0x2f0 stride.
- `src/tables.c` — replaced the enemylist.txt stub with the real
  loader. Reuses the existing `resolve_via_item_state` adapter
  (already wired for enemy.txt and gousei.txt drop resolution).
  Boot trace logs `(sections=S enemies=E drops=D resolved=R
  wisps=W wisp_resolved=WR)`.
- `docs/formats/data-text.md` — appended full enemylist.txt
  section: 5-way line dispatch, sticky state semantics
  (dungeon-slot / section index / enemy slot), section byte-layout
  table, longest-prefix enemy-name lookup vs the pre-baked
  `g_enemy[]`, item-name → id resolution via `tables_item_resolve`,
  faithfully-reproduced quirks (#21 reused, plus new #31/32/33),
  vendor file shape.
- `docs/findings/engine-quirks.md` — added quirks #31 (10 dungeon
  slots reserved, only 6 keyed), #32 (`wisp10:` lands on the `:`
  byte and silent-drops), and #33 (slot-30 terminator hazard
  clobbers slot-0 drop ID — dormant in vendor).
- `tests/test_tables_enemylist.c` — 22 cases: byte-offset
  layout sanity, empty input, comments/blanks, wisp basic /
  empty / wisp10 silent-drop / unknown-item, dungeon-header
  resets section index, `f:N` single-floor, `f:` empty +
  loop-err-16 path, multiple `f:` lines thread sections, enemy
  basic (one drop), multi-drops (up to 3), variant `(N)` suffix,
  count `xN` suffix, longest-prefix wins, unknown enemy name
  skipped, per-line drop reset, NULL resolver yields -1, no-
  trailing-newline EOF, enemies thread across consecutive `f:`
  blocks, end-to-end vendor-shape integration smoke.

**Behavioral validation:**
- 229 unit tests pass under ASan/UBSan (was 207).
- Boot smoke: `data/enemylist.txt — 28281 bytes (sections=100
  enemies=696 drops=1118 resolved=1118 wisps=4 wisp_resolved=4)`.
  100 floor-sections matches the 100 `f:` lines in the vendor
  file. 4 populated wisps = vendor's `wisp3..wisp6` (the parser
  honours `wisp1:`/`wisp2:` ship-empty by leaving slots 0/1 at -1).
  All 1118 drop references resolved to real item ids via the
  shared `g_item` table.

**Engine quirks documented (and faithfully reproduced):**
- **10 dungeon slots, only 6 keyed (#31).** Init scrubs all
  10×60 = 600 sections to `floor_lo = -1`, but the SJIS key
  chain at L2690..L2702 only matches `ダンジョン１..６`. Slots
  6..9 are dead storage with no possible writer.
- **`wisp10:` silent-drops (#32).** Init reserves 10 wisp dwords,
  but the name-copy loop reads from `line[6]` — which is the
  trailing `:` for `wisp10`, terminating the copy immediately.
  Slot 9 storage exists but no `wispN:` line can populate it.
  Vendor only ships `wisp1..wisp6`.
- **Slot-30 terminator hazard (#33).** Engine writes `enemy_id
  = -1` to slot `local_18 + 1` after each enemy line. If a
  section hits 30 enemies, the terminator lands at slot 31's
  enemy_id field — which is the first drop dword of slot 0.
  Vendor never gets close (max ~12 per f-block). Port logs
  overflow + skips the line rather than clobbering drops[0].
- **Per-line drop reset.** drops[slot].item_id[0..2] reset to
  -1 at line start so a line with fewer drops than the previous
  one doesn't inherit stale ids.
- **State sticky across lines.** Dungeon slot, section index,
  enemy slot all persist until the next header. An enemy line
  emitted before any `ダンジョン`/`f:` lands in dungeon 0 /
  section 0 — vendor never does this.
- **`f:N` (no dash) → `floor_hi = floor_lo`.** Dash-scan stops
  on `\r`/`\n`; the second atoi never runs.
- **`f:` (empty) → "loop err 16" + line skipped.** Engine writes
  `atoi("") - 1 = -1` to floor_lo BEFORE bailing — leaving the
  section in a half-init state. Port preserves the write.
- **Effective-exact item-name lookup.** Engine's double-`FUN_00479f4d`
  pattern (memcmp twice, once with each side's strlen) behaves
  like exact match. Port routes through `tables_item_resolve`
  which is strncmp-up-to-32.

**Phase B complete.** All 14 file parsers (counting the 3-file
tutorial loop as one) plus the resolver-wiring follow-up are
landed. Remaining `tables` work for OpenRecet's surface mapping:
`stage.idx` (still a stub at `load_stage_idx`, 22434 bytes —
likely Phase C). Phase 3 next milestone candidates to confirm
with user at session start.

## 2026-05-20 — Phase B [14/15]: `data/news.txt` parser

**Subsystems landed:**
- `src/tables_news.{c,h}` — pure-C parser for FUN_00475270 block
  #11 (`docs/decompiled/by-address/475270.c` L1583..L2236). One
  global at `&DAT_056e0e00`, stride 0xbc (188 bytes), no engine
  cap on count (port reserves 100 slots). Each record carries a
  128-byte body, 16-byte name (parser CAN write 20 → overflows
  into rate, quirk #27), `rate` / `price_lo` / `price_hi`, the
  three lookup results (`attr_mask` / `category` / `item_id`)
  with their sentinel values, the sticky `target_group` from
  `対象者:`, optional `days_lo` / `days_hi`, and the sticky
  `period_start` / `period_end` from `時期:`. `_Static_assert`
  guards on all 13 field offsets.
- `src/tables.c` — replaced the news.txt stub with the real
  loader. Two new resolver adapters `news_resolve_category` and
  `news_resolve_item` prefix-match (engine `FUN_00479f4d`-style)
  against `g_item.categories[].singular` and
  `g_item.records[].singular` respectively, both wired through
  `tables_parse_news`. Boot trace logs
  `(news=N dash=D special=S attr=A category=C item=I)`.
- `docs/formats/data-text.md` — appended full news.txt section:
  file shape, sticky-header semantics, data-row layout with
  optional days range, name-resolution lookup chain (special →
  attr → category → item), record byte-offset table, all faithfully-
  reproduced quirks, vendor file shape.
- `docs/findings/engine-quirks.md` — added quirks #27 (name buffer
  overflow into rate), #28 (prefix-by-name-length lookup, not
  exact match), #29 ("-" rows leave target_group / item_id /
  days_lo / days_hi at BSS-zero), and #30 (body retains trailing
  `\r` on CRLF lines).
- `tests/test_tables_news.c` — 20 cases: empty input, byte-offset
  layout sanity, comments/blanks, `特殊` sentinel, SJIS attr-mask
  hit (`武器` / `防具`), category resolver hit (`Daggers`), item
  resolver hit (`Candy`), lookup-chain precedence (attr wins over
  cat over item), days-range optional, `-` row with all the BSS-
  zero defaults, sticky `target_group` across rows, sticky
  `period_start` / `period_end` across rows, period defaults
  (0, 100) before any header, malformed `時期,A` (no `-`) leaves
  `period_end` unchanged, no-trailing-newline EOF, body retains
  `\r` on CRLF, body strips on LF-only, resolver miss is silent
  (logs but counts), max-records cap, end-to-end vendor-shape
  integration smoke.

**Behavioral validation:**
- 207 unit tests pass under ASan/UBSan (was 187).
- Boot smoke: `data/news.txt — 6342 bytes (news=80 dash=43
  special=2 attr=22 category=12 item=1)`. Each bucket
  cross-checked against an independent Python re-count of the
  vendor file, with the only discrepancy being `アクセサリー` —
  it matches the SJIS attr tag `アク` (0x83 0x41 0x83 0x4e, bit
  0x0010), which the Python re-count's curated attr-tag list
  initially missed. Port matches the engine.

**Engine quirks documented (and faithfully reproduced):**
- **Name buffer can overflow into rate.** Parser caps the
  name-write loop at 20 bytes, but the structural field is 16
  bytes (rate follows at +0x90). For names ≥ 16 bytes the NUL
  terminator lands in rate / price_lo / category. Dormant in
  vendor (longest name = `アクセサリー` at 12 bytes).
- **Lookup chain is prefix-by-name-length.** All three name
  lookups (special / attr / category / item) use
  `memcmp(name, candidate, name_len)`. A short news.txt name
  matches any candidate it's a prefix of. Vendor names always
  fully equal their candidate.
- **`-` rows leave BSS-zero fields.** The `-` branch skips the
  `target_group` / `item_id` / `days_lo` / `days_hi` writes,
  leaving them at memset-zero. Consumers that expect -1 for "no
  match" see 0 for "-" rows.
- **CRLF body keeps trailing `\r`.** Line-collect stores the
  terminating `\r` in the line buffer; body-copy stops at `\0` /
  `\n` but not `\r`. Vendor file is CRLF so every body has a
  trailing `\r` byte.
- **`時期,A` (no `-`) leaves `period_end` unchanged.** Engine
  "loop err 6"; port skips the second atoi via `strchr` miss.

**Note for the next milestone:**
- Phase B 15: `enemylist.txt` (28281 bytes — substantially
  larger than news.txt). Confirm with user at session start —
  enemylist.txt's parser block is much further down in the
  binary and may need its own discovery doc.

## 2026-05-20 — Phase B [13/15]: `data/event.txt` parser

**Subsystems landed:**
- `src/tables_event.{c,h}` — pure-C parser for FUN_00475270 block #10
  (`docs/decompiled/by-address/475270.c` L1521..L2235). 4 in-town
  location categories (広場/市場/教会/酒場), each with up to 100
  records (50-dword stride = 200 bytes); each record carries an
  event id, a "flag to set on trigger", 4 hex-encoded prereq slots
  (lowercase `0..9/a..f`, with sticky `-` → -1), first/max weekday-
  of-day index (NOT a bitmask like kyaku.txt — single 0..3 indices
  for 朝/昼/夕/夜), 20 day-range pairs, a `loop_min` gate, and a
  `decay_or_max` field that pre-bakes to 100000 for the seed record
  and 0 for all parsed records. `_Static_assert` guards on every
  field offset so the layout stays byte-identical to the consumer
  `FUN_0045de68`'s negative-offset reads.
- `src/tables.c` — replaced the event.txt stub with the real loader.
  Boot trace logs
  `(hiroba=H ichiba=I kyokai=K sakaba=S with_prereqs=N)`. No
  resolver wiring needed — `event.txt` has no cross-table lookups.
- `docs/formats/data-text.md` — appended full event.txt section:
  category-header table, record layout with field offsets, data-line
  shape annotated, prereq encoding (hex + sticky -), weekday-of-day
  tag table with the 1-byte-mismatch quirk, day-pair format,
  pre-baked default record, all faithfully-reproduced quirks,
  vendor file shape.
- `docs/findings/engine-quirks.md` — added quirk #26 ("`event.txt`'s
  weekday-tag mismatch advances 1 byte, not 2") with the engine
  decomp snippet and dormant-but-real explanation.
- `tests/test_tables_event.c` — 15 cases: empty-seeds-default,
  byte-offset layout sanity, comments/blanks, basic 広場 record,
  prereq hex+minus, time_first/max tracking, time_max clamps to
  no-higher, unknown-only tokens leave 0/0, loop_min atoi, 20-pair
  cap, all 4 categories dispatched, pre-header data-line goes to
  広場, decay_or_max=100000 only for seed, no-trailing-newline,
  vendor-shape integration smoke.

**Behavioral validation:**
- 187 unit tests pass under ASan/UBSan (was 172).
- Boot smoke: `data/event.txt — 8901 bytes (hiroba=39 ichiba=9
  kyokai=9 sakaba=19 with_prereqs=76)`. The four counts match an
  independent Python re-count of the vendor file's headered
  sections + data lines, including the pre-baked seed contributing
  1 to 広場. Every record has `prereq[0] >= 0` (vendor convention:
  the "must NOT be set" flag is always populated).

**Engine quirks documented (and faithfully reproduced):**
- **Pre-baked record 0 of category 0.** Before parsing, the engine
  hand-writes a "default 広場 event" with id=0x0b, prereq[0]=0xa3,
  time 0..1, day range (0,40), loop_min=0, decay_or_max=100000.
  Sets `counts[0] = 1`, so the first parsed 広場 line lands at
  slot 1.
- **Lines before any header dispatch to 広場.** Init leaves
  `local_18 = 0` (= 広場). Vendor data has 広場 as the first
  header so this is dormant.
- **Hex-only prereq with sticky `-`.** `:`-delimited fields accept
  lowercase `0..9/a..f` only; any byte that's not hex/`-`/`:` is
  silently skipped (e.g. leading spaces). A `-` anywhere in a
  field's tail nukes that field to -1 regardless of any hex value
  accumulated before it. So `100` = 0x100 = 256, `-1`/`-2`/`f-f`
  all = -1.
- **Weekday-tag mismatch advances 1 byte, not 2.** New quirk #26.
  Unknown 2-byte SJIS chars get scanned twice (once at byte 0,
  once at byte 1). Dormant in vendor data thanks to full-width-
  space padding `81 40`.
- **`time_first == 0` is overloaded.** "No matched token" and
  "first matched token was 朝" both result in `time_first = 0`.
  Consumer interprets as "morning-only" in both cases.
- **End-of-list sentinel.** Loader writes `id = -1` to the slot
  one past `counts[cat]` for each category — the consumer's loop
  terminator.

**Note for the next milestone:**
- Phase B 14: `news.txt` (6342 bytes, ~330 C lines in 475270.c
  block #11) — pre-categorised by `対象者:`/`時期:` headers, then
  `品名,カテゴリ,価格-高値,日数-日数` data lines (5 fields with
  comma + dash separators). Larger than event.txt, but still
  smaller than the average tables file.

## 2026-05-20 — Phase B [12/15]: `data/kyaku.txt` parser

**Subsystems landed:**
- `src/tables_kyaku.{c,h}` — pure-C parser for FUN_00475270 block #4
  (`docs/decompiled/by-address/475270.c` L469..L832). 18 active
  records out of 50 in vendor data; each record carries a
  singular/plural name, name-table index, 2-axis attribute pair,
  up-to-20 preferred item categories, preferred-attribute bitmask
  (uses the same 16-tag SJIS table as oder/item), budget range,
  activity-time mask (朝/昼/夕/夜 → bits 1/2/4/8), 6 haggle-tuning
  ints, and a per-customer dialog-file path.
- `src/tables.c` — replaced the kyaku.txt stub with the real loader.
  New `resolve_via_item_category` adapter resolves `好き種類:` lines
  against `g_item.categories[].singular` (populated earlier by
  item.txt's category headers). Boot trace logs
  `(customers=N like_kinds=K with_budget=B)`.
- `docs/formats/data-text.md` — appended full kyaku.txt section: per-
  line dispatcher table, header singular/joint quirk details, the
  activity-time and preferred-attribute token tables, all 8
  faithfully-reproduced quirks, vendor file shape.
- `tests/test_tables_kyaku.c` — 23 cases covering: empty input,
  comments / blanks, header singular-only + with-plural, attr X/Y
  (full + empty), budget range (full + empty), like-kind resolver
  hit / null-resolver-skip / 20-cap, like-attr SJIS mask, the
  `嫌い:` orphan-noop, file_path copy, activity-time (all 4 tokens,
  partial, unknown token), atoi scalars, lines-before-header
  dropped, no-trailing-newline, multi-customer threading,
  resolves-via-item-category end-to-end with a hand-populated
  `item_state_t`, and a vendor-shape integration smoke.

**Behavioral validation:**
- 172 unit tests pass under ASan/UBSan (was 149).
- Boot smoke: `data/kyaku.txt — 7603 bytes (customers=18
  like_kinds=111 with_budget=15)` matches the vendor file (manual
  count of `好き種類:` lines totals 111; only Recette / Tear /
  Euria have empty `予算:` → 18 - 3 = 15 with-budget).

**Engine quirks documented (and faithfully reproduced):**
- **`嫌い:` is an orphan match.** The 5-byte `嫌い:` key match has
  an empty body — match-but-discard. Almost certainly a dialled-back
  feature; vendor data still ships dozens of `嫌い:` lines but nothing
  consumes them.
- **Header singular/joint write-position reset.** On `NNN:S#P` the
  joint cursor resets to offset 0 at the `#`, so the plural
  *overwrites* joint[0..] starting from the beginning. If plural is
  shorter than singular, the tail of singular leaks into joint —
  but vendor data never triggers (all plurals ≥ singular length).
- **Singular NUL at off-by-five.** Engine's `puVar14[iVar17 + 5] = 0`
  writes NUL at `singular[iVar17 + 1]`, NOT `singular[iVar6 + 1]`.
  For `#`-containing headers it lands several bytes past singular's
  end. Harmless thanks to BSS zero-init.
- **Header gated by leading `0`.** Dispatcher only tries the 50-iter
  `%03d:` match if `line[0] == '0'`. Records 0..49 always start
  with `0` so this is a perf optimisation in practice; record IDs
  ≥ 100 would be silently ignored.
- **`属性:` / `予算:` unbounded delimiter scans.** Once the first
  numeric is parsed, the engine walks forward looking for `,` /
  `-` with NO upper bound. Vendor data always has the delimiter;
  the port also stops at NUL.
- **`好き種類:` cap of 20** + MessageBoxA on overflow / unknown
  category. Port logs to stderr.
- **Lines before any header are silently dropped** (engine's
  `local_14 < 0` sprintf-to-discarded-local branch).

**Resolver wiring:**
- `好き種類:` resolves through `resolve_via_item_category` (new in
  `src/tables.c`) — different from the existing `resolve_via_item_state`
  (which probes `g_item.records[].singular` for full item names).
  Kyaku resolves against the **category-name** table at
  `g_item.categories[].singular`, populated by item.txt's
  `:Category#(tag)` headers. The 111 vendor `好き種類:` lines all
  resolve successfully against the populated category table.

**Note for the next milestone:**
- Phase B 13: `event.txt` (8901 bytes, ~62 C lines in 475270.c
  block #10) — likely the next-easiest remaining file. Or `news.txt`
  (6342 bytes, 655 C lines — larger but more boxed-in to a single
  format). Confirm priority with the user.

## 2026-05-20 — Phase B [11/15]: resolver-wiring follow-up

**Subsystems touched:**
- `src/tables_enemy.{c,h}` — `tables_parse_enemy` gains an
  `enemy_resolve_fn (resolve, user)` pair, replacing the dead-stub
  `lookup_item_id` that always returned -1. NULL resolver collapses
  to the previous behaviour (tests use this).
- `src/tables_gousei.*` — already accepted the resolver; no change.
- `src/tables.c` — new `resolve_via_item_state` adapter wires
  `tables_item_resolve(&g_item, name)` into both `load_enemy_txt`
  and `load_gousei_txt`. Boot trace now reports resolution counters
  (`drops_resolved`, `outputs_resolved`, `ingredients_resolved`).
- `tests/test_main.c` — registry is X-macro-driven now (separate
  cleanup commit). 149 tests pass (was 147): two new tests cover
  the resolver wiring end-to-end (`tables_enemy_drop_resolves_via_callback`
  via stub; `tables_gousei_resolves_via_item_state` via a real
  hand-populated `item_state_t`).

**Observed boot deltas** (vendor data):
- `enemy.txt — drops_resolved=70` (was 0 — 54 enemies × ≤2 drops).
- `gousei.txt — outputs_resolved=101 ingredients_resolved=268`
  (was 0 — every recipe output name has a matching item.txt singular,
  so 100% of outputs resolve).

**Out of scope (still deferred):**
- `oder.txt` attribute-table fallback — its name table at
  `&DAT_0963e5f8` is populated by item.txt's category-header path,
  so the lookup already works; no rewire needed.
- Drop-name → item-id misses for the ~38 enemy slots that still
  resolve to -1. These are vendor-data spelling mismatches and need
  per-name investigation; out of scope for the wiring pass.

## 2026-05-20 — Phase B [10/15]: `data/item.txt` parser

**Subsystems landed:**
- `src/tables_item.{c,h}` — pure-C parser for FUN_00475270 block #3
  (`docs/decompiled/by-address/475270.c` L428..L468 main dispatch +
  L815..L829 cross-block record fallback reached via
  `goto LAB_00476d04`). The two sub-parsers are FUN_00491044
  (category header, 81 bytes) and FUN_004912de (item record, 820
  bytes). 716-byte record layout (stride 0x2cc) populated end-to-end:
  rank, price, atk, def, mt, mf, attr_mask (incl. category-class OR
  via FUN_0049eb2a), equip_class (FUN_0049ed75), stock_info[9]
  (FUN_00491095 — 7 SJIS tags incl. `ダ`'s ×10-if-<10 quirk),
  aud_mask (FUN_0049e849 — 11 SJIS audience tags including `男`/`女`
  composites), singular[64], plural[64], desc_line1[256],
  desc_line2[256]. Static asserts validate every offset.
- `src/tables.c` — replaced the item.txt stub with the real loader.
  Boot trace logs `(items=N max_id=M equippable=K cats=C)`.
- `docs/findings/item-table.md` — captures the chained-dispatcher
  discovery (the cross-block `goto LAB_00476d04` is real, not a
  decompiler artifact), the scratch-buffer flow (FUN_00491044 writes
  scratch consumed by FUN_004912de's sprintf copies), the per-record
  byte layout, and the resolver implications for the three already-
  ported parsers that defer item-name lookup (oder, enemy, gousei).
- `docs/formats/data-text.md` — appended a full item.txt section:
  per-line dispatcher table, 12-field record format, attribute /
  stock / audience tag tables, the `##`-makes-desc1-the-real-content
  semantics, vendor file shape.
- `tests/test_tables_item.c` — 23 cases covering: empty input,
  comment/blank/indent-space skipping, basic record (with and without
  `+` plural), full stat fields, category header routing,
  multi-category index threading, attribute-mask + category-class
  OR, audience tags (全 → 0xff, 男 → 0x55, リ → 0x01,
  empty-field → 0xff), stock tags (在庫(N) basic + ダ(N) ×10 quirk),
  out-of-range item_id dropped, no-trailing-newline, description-
  line1+line2 split on embedded `#`, phase-2 `/` truncation,
  unknown-line stderr fallback, resolver lookup, slot cap, and a
  vendor-shape integration test against
  `/tmp/openrecet-extract/data/item.txt`.
- `tools/analyze/pe.py` — added the `bytes` subcommand earlier;
  reused here to identify the dispatcher sentinels `':'` at
  `0x5cacf0` and `' '` at `0x5cacf4`, plus the stock-info /
  audience SJIS tag tables.

**Behavioral validation:**
- 147 unit tests pass under ASan/UBSan (was 124).
- Boot smoke: `data/item.txt — 121998 bytes (items=571 max_id=5408
  equippable=331 cats=33)` matches the vendor file's actual counts
  (Python analysis: 571 records, 33 categories, IDs 0..5408).
- ASan caught one early-iteration bug: `item_class_bits` had a
  hand-written length table (`{ "Arm Parts", 12, ... }`) that
  memcmp'd past the C string literal's bounds. Fixed by switching to
  `strlen(.name)` + exact-NUL terminator check. Test for this is
  implicit in the vendor-shape run, which would crash under ASan if
  the OOB read reappeared.

**Engine quirks documented:**
- **Cross-block dispatcher goto.** The non-`:` line path inside
  item.txt's loop is reached via `goto LAB_00476d04` that physically
  lands inside the next block's (kyaku.txt) function body. Real code
  layout, not a decompiler artifact — the port linearises it.
- **Most-recent-header semantics.** Category headers don't index
  into the per-category table directly; the next item record copies
  the scratch buffer into `categories[item_id/100]`. Vendor files
  respect the convention; an adversarial reorder would scramble the
  category-name lookup.
- **Phase-1-immediate-`#` empties desc_line1.** Vendor `##` between
  AUD and DESC means AUD is empty (engine ORs `aud_mask |= 0xff`)
  and DESC1 starts AT the byte after the second `#`. desc_line1
  ends up with the first half of the description; desc_line2 gets
  the second half after the `#` between them.
- **Description phase 2 ends on `/`.** A literal `/` in the second
  description line truncates the field. Phase 1 has no such check.

**Note for the next milestone:**
- Resolver wiring (Phase B 11 follow-up) is now unblocked. A single
  pass through `src/tables.c` can wire `tables_item_resolve` into
  the deferred hooks of `tables_parse_enemy` (drop refs, currently -1)
  and `tables_parse_gousei` (ingredient/output IDs, currently -1).
  `oder.txt`'s attribute-table lookup doesn't actually need
  resolution — its `attr_index = -1` placeholder was a misread; the
  oder parser already references the singular-name table via
  `oder_attr_hash`, and the table will be populated automatically
  now that item.txt has been parsed. Cleanup is mostly removing the
  TODO comments from `src/tables.c`.

---

## 2026-05-20 — Phase B [9/15]: `data/gousei.txt` parser

**Subsystems landed:**
- `src/tables_gousei.{c,h}` — pure-C parser for FUN_00475270 block
  #13 (LAB_004790cd / `docs/decompiled/by-address/475270.c`
  L2402..L2579). 12-dword (0x30-byte) record layout: output_id, rank,
  ingredient_id[5], ingredient_count[5]. Header-vs-recipe dispatch
  on the 7-byte SJIS `ランク:` prefix. Recipe lines skip the 5-byte
  `NNNN:` prefix wholesale (engine: `pcVar16 = local_27c + 0x25`),
  then walk colon-separated fields with `#N` count modifiers.
- `src/tables.c` — replaced the gousei stub with a real loader.
  Threads a NULL item-name resolver for now (item.txt parser hasn't
  landed); when it does, tables.c will pass a real callback into
  `tables_parse_gousei` without touching the parser. Boot trace
  logs `(recipes=N max_rank=M)`.
- `docs/formats/data-text.md` — appended a full gousei section:
  per-record layout, header dispatch, the discarded 4-digit prefix,
  the ing1-write quirk, the exact-name lookup, the index-0
  MessageBox quirk, the 200-record cap, vendor file shape.
- `docs/findings/engine-quirks.md` — added quirk #23: the
  `Master's Plate` recipe line ships without a trailing `:`, which
  trips the engine's unbounded `:` hunt past the line terminator and
  into surrounding memory. Record still commits; port detects EOL
  in the hunt and finalises the column cleanly.
- `tests/test_tables_gousei.c` — 15 cases covering empty input,
  comment/blank skipping, basic recipes, rank header dispatch,
  rank-0 recipes preceding any header, prefix-discarded behaviour,
  3- and 5-ingredient widths, NULL-resolver fall-through, unknown-
  name → -1, the EOL-without-trailing-':' recovery, no-trailing-
  newline, the 200-record cap, embedded-NUL early-exit, and a
  vendor-shape integration test.

**Behavioral validation:**
- 124 unit tests pass under ASan/UBSan (was 109).
- Boot smoke: `data/gousei.txt — 6252 bytes (recipes=101 max_rank=5)`
  matches the vendor file's actual recipe count (22+22+17+19+21 = 101
  across ranks 1..5). Pre-fix, my parser was reporting 100 — the
  missing recipe was the Master's-Plate-without-trailing-':' line,
  which my -1-return path was silently dropping; chased it down via
  per-line debug instrumentation, then replaced the bail with an
  EOL-aware fall-through.

**Note for the next milestone:**
- Item resolver hook is now the gating dependency for *several* of
  the already-ported parsers (oder.txt attribute lookup, enemy.txt
  drop refs, gousei.txt output/ingredient IDs). Once item.txt's
  parser is in, a single resolver callback wired into each loader
  will populate the long-deferred ID fields without re-touching the
  parsers.

---

## 2026-05-20 — Phase B [8/15]: `data/tuto[123].txt` parser

**Subsystems landed:**
- `src/tables_tuto.{c,h}` — pure-C parser for FUN_00475270 block #15
  (L2898..L3123). The three tutorial scripts (`tuto1.txt` /
  `tuto2.txt` / `tuto3.txt`) share a single 296-byte-per-record
  array (`g_tuto[600]`). Per-line CSV with a 16-token opcode
  dispatch (ASCII tokens `CHR0`/`CHR1`/`TAGD`/`PRID`/`PRIA`/`BUN0`/
  `GOTO`/`TAGN`/`TOUT` and SJIS keywords `値段`/`高く`/`値引`/`値上`/
  `アイテム`/`剣選択`/`初期金額決定`). Two payload families: 1 int +
  text for CHR0/CHR1, 7 ints for the price/branch opcodes, none for
  the rest. Handles the `id == -1` sentinel and `id <= -2`
  text-only branches faithfully.
- `src/tables.c` — replaced the tuto stub-loop with a real loader.
  Mirrors the engine's hard-coded 3-file iteration (no early-exit
  on miss) and logs `(records=N)` with a ⚠ when N exceeds the
  50-slot parser cap (which it does on all three vendor files).
- `docs/formats/data-text.md` — appended a full tuto section:
  opcode table, record layout, the parser-vs-consumer stride
  mismatch, vendor-data overflow numbers, and the final
  cross-overwritten array state.
- `docs/findings/engine-quirks.md` — added quirk #22 (parser stride
  50 vs consumer stride 200, both pointing at `&DAT_005d1fc8`;
  three of four `FUN_00461bf6` callers push `2` so the consumer
  reads a never-written region).
- `tests/test_tables_tuto.c` — 18 cases covering empty input,
  blank/comment skipping, every ASCII opcode, every SJIS opcode,
  the `id < 0` branches, the 7-int reader with short-arg fallback,
  the file_index×50 stride, the 50-slot overflow, and a vendor-
  shape integration test.

**New persistent tooling:** `tools/analyze/pe.py` — PE32 helper
module + CLI for the unpacked vendor exe, used for VA → file offset
mapping, NUL-terminated cp932 string dumps, raw byte / blob
extraction, and call-site discovery with PUSH-imm decoding.
Replaces the ad-hoc inline Python scripts that kept getting
reinvented for each RE session. `docs/AGENT-WORKFLOW.md` got a new
"Persistent analysis tooling" section pointing at it.

**Engine fidelity divergences (documented):** the 7-int reader on
short lines reads stack garbage in the engine; our port zeros the
line buffer between records so missing args read as 0 (benign —
gameplay code only uses `args[0]` for `GOTO`). The parser-vs-
consumer stride mismatch (quirk #22) is preserved on the parser
side; the consumer port will inherit whatever the engine actually
does at runtime.

**Boot trace** (smoke test, vendor data):
```
tables: data/tuto1.txt — 8978 bytes (records=135 ⚠ overflows 50-slot cap)
tables: data/tuto2.txt — 5828 bytes (records=90 ⚠ overflows 50-slot cap)
tables: data/tuto3.txt — 4064 bytes (records=60 ⚠ overflows 50-slot cap)
tables: tuto overflow — 3/3 files exceed the 50-slot parser cap (engine quirk: stride mismatch vs consumer)
```

**Tests:** 109 pass (was 91), 0 fail, 0 skip.

**Remaining Phase B order:** `gousei.txt → kyaku.txt → event.txt → news.txt → stage.idx → enemylist.txt → item.txt`.

## 2026-05-20 — Phase B [7/15]: `data/enemy.txt` parser

**Subsystems landed:**
- `src/tables_enemy.{c,h}` — pure-C parser for FUN_00475270 block #5
  (L834..L1026). 64 fixed enemy records at `&DAT_005c23f0` (stride
  0x68 = 104 bytes). Per-line **longest-common-prefix** match
  against the pre-baked record names, then 6 ints (HP/EXP/AT/DF/MA/MD)
  + 2 drop-item name lookups; both drops reset to -1 at line start.
  Pre-baked NAMES + boss flags live in `.data` (extracted from
  `vendor/unpacked/recettear.unpacked.exe` at file offset
  `0x1c0bf0`) and are populated via `tables_enemy_init` before the
  parser runs.
- `src/tables.c` — replaced the enemy.txt stub with a real loader.
  Init-then-parse pattern: call `tables_enemy_init(g_enemy)` to
  copy the 64 names + flags from `.data`, then `tables_parse_enemy`
  to overlay the stats. Boot trace logs `(enemies=N bosses=M)` with
  a counter that handles outlier vendor rows.
- `docs/formats/data-text.md` — appended a full enemy.txt section:
  line shape, 0x68-byte record layout, longest-prefix lookup with
  worked examples, pre-baked-record metadata, engine quirks, and
  lnkdatas-vs-overlay vendor shape (2801 vs 3589 bytes).
- `docs/findings/engine-quirks.md` — added quirk #21 (`enemy.txt`
  unmatched lines fire MessageBoxA on every boot of the original
  exe; `アルマ*` lines collapse onto a single record via the
  alias-prefix path).
- `tests/test_tables_enemy.c` — 10 cases: pre-baked init, basic
  record, longest-prefix wins (アーリマン緑 over アーリマン), shorter
  prefix when no longer match available, comments + blank lines
  skipped, per-line drop reset, unknown-name silently skipped,
  placeholder records (`name = " "`) skip match, no-trailing-newline,
  vendor-shape end-to-end with mixed-prefix routing.

**Engine fidelity divergences (documented):** the port silently
skips unmatched lines (engine pops a blocking MessageBoxA on every
one — vendor data triggers this 9 times per boot via the overlay
file's late-content lines). Drop-name → item-id resolution is
deferred until `item.txt` lands (slot #3, still a stub) — drops
resolve to -1 unconditionally. The seven runtime floats at
+0x44..+0x5f (collision/sprite-scale data, populated by
not-yet-ported runtime code) are left at zero in the port; the
engine ships them with a baked snapshot in `.data` that the parser
overwrites for stats but not these.

**Boot verification:** stderr now shows
`tables: data/enemy.txt — 3589 bytes (enemies=54 bosses=6)` against
the bmpdata overlay (which `storage_read` picks first). The 54
records match the count of unique pre-baked record names that the
overlay's 67 data lines route into via longest-prefix match. The
6 bosses come straight from the pre-baked flags table.

**Test status:** 91 tests pass (up from 81), no fails, no skips.

## 2026-05-20 — Phase B [6/15]: `data/snews.txt` parser

**Subsystems landed:**
- `src/tables_snews.{c,h}` — pure-C parser for FUN_00475270 block #12
  (L2238..L2401). Two unrelated globals populated from one file: a
  flat 64-slot name table keyed by 3-digit ID (`NNN:<text>` lines)
  and a 10×30 grid of floor-range sections keyed by SJIS dungeon
  names (`ダンジョン1`..`ダンジョン6`) with per-section weighted
  entry lists (`NNN,W` and `NON,W`). Only 6 of the 10 outer dungeon
  slots are reachable; the other 4 stay empty.
- `src/tables.c` — replaced the snews.txt stub with a real loader.
  Boot trace logs `(names=N sections=M)`, where `sections` counts
  records with non-sentinel `floor_start`.
- `docs/formats/data-text.md` — appended a full snews.txt section
  with line-shape table, the SJIS dungeon-key bytes, record layout
  for both globals, engine quirks (including the dungeon-transition
  off-by-one), and vendor-file shape with per-dungeon f: counts and
  weights.
- `docs/findings/engine-quirks.md` — added quirk #20 (snews.txt
  dungeon-transition floor-range corruption) with the full
  pointer-juggling story.
- `tests/test_tables_snews.c` — 10 cases: empty (sentinel init),
  name table (basic, empty value, overlong→truncated), comments +
  blanks skipped, single dungeon + section (with engine off-by-one
  verified), multiple sections within one dungeon, dungeon
  transition floor-end corruption (the quirk pinned in a dedicated
  test), entry-slot overflow dropped at port cap, and a full
  vendor-shape end-to-end with spot checks on every f:-line's
  landing position.

**Engine fidelity divergences (documented):** the dungeon-transition
floor-range corruption (quirk #20) is reproduced faithfully — the
first `f:N-M` line of every new dungeon writes its floor info to the
*previous* dungeon's last section before advancing. Vendor data is
structured so this is benign; consumers querying floor ranges still
see plausible matches. Port adds safety caps for overlong names
(>= 64 chars), name-table OOB IDs, and per-section entry-slot
overflow (>20 entries).

**Boot verification:** stderr now shows
`tables: data/snews.txt — 2230 bytes (names=25 sections=10)`. The 10
sections matches the trace: 11 `f:` lines across 6 dungeons, with
the off-by-one shifting the last-section-of-each-dungeon writes onto
the next-dungeon's first section, leaving dungeon 6's section [5][0]
with floor_start = -1 (no successor to write over it).

**Test status:** 81 tests pass (up from 71), no fails, no skips.

## 2026-05-20 — Phase B [5/15]: `data/chara.txt` parser

**Subsystems landed:**
- `src/tables_chara.{c,h}` — pure-C parser for FUN_00475270 block #6
  (L1030..L1146 outer + L76547..L76593 LAB_00477931 continuation).
  Two interleaved CSV sub-blocks share the same 8 records:
  `000:`..`007:` populates base stats (10 fields, 7 ints + 3 floats);
  `100:`..`107:` populates the level-100 endpoints (6 ints, permuted
  AT/DF/MT/MF/HP/SP → hp_lv100/sp_lv100/at_lv100/.../mf_lv100).
  Engine init seeds nine of the ten base fields per record (LV=1,
  HP=50, SP=30, AT=10, DF=13, MT=5, MF=10, move=0.15f, dash=0.20f);
  the port memsets to zero first so crit_rate and all lv100 stats
  start at 0 — a harmless superset.
- `src/tables.c` — replaced the chara.txt stub with a real loader.
  Heuristic for the boot trace: `level_threshold != 1` flags a
  parsed record (default is 1; vendor unlock-levels 1/8/10/15/20/30
  store as 0/7/9/14/19/29, none equal to 1). Boot trace now logs
  `(adventurers=N lv100=M)`.
- `docs/formats/data-text.md` — appended a chara.txt section with
  line-shape table, record layout, field-order permutation
  (file order vs in-memory layout for both sub-blocks), defaults
  table with bit-exact float values, engine quirks, the 10×8
  parse-loop overrun bug, and full vendor-shape table for the 8
  adventurers (Louie through Arma).
- `tests/test_tables_chara.c` — 9 cases: empty (defaults only),
  defaults bit-exact (0x3e19999a / 0x3e4ccccd match `0.15f` / `0.20f`
  byte-for-byte), basic record, lv100 alone, both blocks combined,
  comments skipped, OOR-index 008/009/108/109 guarded (no OOB
  write), lv100 field permutation with distinct sentinels,
  vendor-shape end-to-end with spot checks on Louie/Griff/Arma.

**Engine fidelity divergence (documented):** the engine's parse
loop iterates 10 times per sub-block even though only 8 records are
initialized — a 2-record overrun bug that would write into the
adjacent `g_models[0..1]` globals at `&DAT_073ae258` if chara.txt
contained any `008:` / `009:` / `108:` / `109:` lines. Vendor data
ships only `000:`..`007:` and `100:`..`107:`, so the bug is
dormant. The port caps the inner match loop at `CHARA_COUNT` and
silently drops out-of-range indices.

**Boot verification:** stderr now shows
`tables: data/chara.txt — 1868 bytes (adventurers=8 lv100=8)`,
matching the vendor file's 8 adventurer rows + 8 lv100 endpoint
rows. All other stubs continue to log as before; tutorial loop
still stops correctly at `tuto4.txt`.

**Test status:** 71 tests pass (up from 62), no fails, no skips.

## 2026-05-20 — Phase B [4/15]: `data/model.txt` parser

**Subsystems landed:**
- `src/tables_model.{c,h}` — pure-C parser for FUN_00475270 block #9
  (L1422..L1520). Fixed array of 20 records at `&DAT_073ae258` (stride
  0x2b8 bytes). Per-line dispatch: `no:N` sets current model index
  (atoi), `fname:` copies the `.x` filename, `NN:` (00..19) copies a
  bone/attachment-point name and increments `count`. Engine quirks
  faithfully reproduced: `local_c` defaults to 0 (writes before `no:`
  go to record 0); `used[slot] = 1` and `count++` fire unconditionally
  on every matching `NN:` line (no gate on `!used[slot]`); all 20 slot
  prefixes checked on every line. Safety divergences: fname + point
  names truncated at 31 chars + NUL to prevent field-overflow into
  adjacent record fields; out-of-range `no:N` (N < 0 or N ≥ 20) skips
  subsequent writes rather than computing an out-of-bounds pointer.
- `src/tables.c` — replaced the model.txt stub with a real loader.
  Counts `defined` (records with `count > 0`) and `max_points` (max
  `count` value across all records). Boot trace now logs
  `(models=N max_points=M)`.
- `docs/formats/data-text.md` — appended a model.txt section with
  line-shape table, record layout, engine quirks and safety
  divergences, and vendor-file shape including the out-of-order
  indices (17/18 appear swapped in the file).
- `tests/test_tables_model.c` — 9 cases: empty, basic one record,
  index threading (records 0 and 5), comments/blanks skipped, fname
  before any no:, repeated-slot count increment, overlong fname
  truncation (count field not corrupted), out-of-range no: skipped
  (no OOB write), vendor-shape end-to-end fixture with all 17 models
  and spot-checks on fname, point names, and gap indices 9/16/19.

**Engine fidelity divergence (documented):** the engine's write cap
for both fname and point names is 0x100, but the fname field is only
0x20 bytes before the `count` field — an overlong fname would
silently corrupt adjacent fields. Our port truncates at
`MODEL_DEF_NAME_MAX - 1 = 31` chars. Out-of-range `no:N` indices
are also guarded (engine would compute an out-of-bounds pointer on
`no:25` etc.). Vendor data has fnames ≤ 12 chars and indices 0..18,
so both guards are dormant against real input.

**Boot verification:** stderr now shows
`tables: data/model.txt — 1758 bytes (models=17 max_points=8)`,
matching the vendor file's 17 defined records and 8-point maximum
(kani models at indices 10 and 11). All other stubs continue to log
as before; tutorial loop still stops correctly at `tuto4.txt`.

**Test status:** 62 tests pass (up from 53), no fails, no skips.

## 2026-05-20 — Phase B [3/15]: `data/oder.txt` parser

**Subsystems landed:**
- `src/tables_oder.{c,h}` — pure-C parser for FUN_00475270 block #8
  (dispatch L1378..L1421 + inner CSV loop reached via
  `goto LAB_00477ffe` at L1813..L1931). Two parse phases plus a
  `LV:`-header dispatch: each data row is `<singular>,<plural>,
  <attribute>`, where field 1 writes at column position into the
  record (engine quirk faithfully reproduced with a safe truncation
  guard), field 2 writes sequentially after the first comma, and
  field 3 is hashed against a 16-tag SJIS attribute table at
  `&DAT_005fd7fc`. Record stride 0x4c (76 bytes) matching the engine.
- `src/tables.c` — replaced the oder.txt stub with a real loader.
  Boot trace now logs `(orders=N max_lv=M)`.
- `docs/formats/data-text.md` — appended an oder.txt section with
  line-shape table, record layout, the full 16-tag attribute table
  (SJIS bytes + kanji + romaji + meaning), inner-loop quirks
  (100-char cap, tab skipping, column-position writes), and the
  fallback name-table lookup that we intentionally suppressed
  until `item.txt` lands.
- `tests/test_tables_oder.c` — 9 cases: empty, single record, LV
  threading across data lines, all 16 SJIS tags → expected bits,
  English fallback (mask=0, attr_index=-1), tab skipping inside
  fields, 100-char inner-loop cap, no-trailing-newline EOF,
  vendor-shape end-to-end fixture with mixed SJIS/English rows
  across LV groups 1, 2, and 5.

**Engine fidelity divergence (documented):** the engine's fallback
linear search through `&DAT_0963e5f8` (item-name table, populated
by item.txt) is deferred — populated as `attr_index = -1`. When
item.txt parses we'll add a name-lookup callback hook. The
engine's MessageBoxA on unknown attributes is intentionally
suppressed so the port doesn't pop up "属性不明な登録" on boot.

**Boot verification:** stderr now shows
`tables: data/oder.txt — 1686 bytes (orders=24 max_lv=5)`,
matching the vendor file's 24 records across LV groups 1-5. All
other 16 stubs continue to log as before; tutorial loop still
stops correctly at `tuto4.txt`.

**Test status:** 53 tests pass (up from 44), no fails, no skips.

## 2026-05-20 — Phase B [2/15]: `data/config.idx` parser

**Subsystems landed:**
- `src/tables_config.{c,h}` — pure-C parser for FUN_00475270 block #2.
  Five live keys (`kanjioff`, `edgewi`, `effectmode`, `edgedel`, `font`)
  + one dead key (`makefont` — the engine matches 8 bytes against the
  bare word but assigns to nothing; we mirror the dead check). The
  `font:` value is copied as raw bytes into a 256-byte fixed buffer
  with safe truncation on overlong input.
- `src/tables.c` — replaced the config.idx stub with a real loader.
  Path-mismatch quirk still sidestepped via the read-side spelling
  (`"data/config.idx"`) for both `storage_get_size` and `storage_read`.
- `docs/formats/data-text.md` — appended a config.idx section with
  full key table, dead-makefont quirk, and the line-terminator
  handling difference from buysell.txt.
- `tests/test_tables_config.c` — 7 cases: empty input, all five
  live keys parsed together, `makefont:` no-op, SJIS font name
  (`ＭＳ Ｐゴシック`), font over-length truncation at the 256-byte
  cap, comment-only file (everything `/`-prefixed → all defaults),
  vendor-shape end-to-end (only `edgewi=2 edgedel=6` active).

**Boot verification:** stderr now shows
`tables: data/config.idx — 950 bytes (kanjioff=0 edgewi=2 edgedel=6 effectmode=0 font=(default))`,
matching the shipping vendor file's active key set exactly.

**Test status:** 44 tests pass (up from 37), no fails, no skips.

## 2026-05-20 — Phase B [1/15]: `data/buysell.txt` parser

**Subsystems landed:**
- `src/tables_buysell.{c,h}` — pure-C parser for FUN_00475270 block #7.
  Mirrors the engine's "match every prefix on every non-comment line"
  structure with five key forms: `ok:` (debug flag), `客番号:` / `種類:`
  (SJIS scalars), and `msg%02d:` / `rmsg%02d:` (two 20-int arrays).
  Engine-global instance `g_buysell`; tests use the out-parameter form.
- `src/tables.c` — replaced the buysell stub with a real loader that
  storage_reads the file, calls `tables_parse_buysell`, and logs the
  three scalars to the boot trace.
- `docs/formats/data-text.md` — new format-spec doc for the
  `data/*.txt` + `idx/*.idx` group. Documents shared conventions
  (Shift-JIS, CRLF, leading-`/` comments, two format families) plus a
  full section for buysell.txt (key table with byte-level SJIS
  identification, engine-side global addresses, the
  rmsg-before-msg in-memory layout quirk, vendor file sample).
- `tests/test_tables_buysell.c` — 8 cases covering empty input,
  comment-only files, the `ok:` toggle, the two SJIS scalar keys
  (using the exact byte sequences from the engine's `.data`),
  msg/rmsg arrays at boundary indices 0 and 19, EOF-without-newline,
  embedded-`\0` early-termination, and a vendor-shape end-to-end
  fixture that reproduces the actual file's CRLF + SJIS + comment
  layout with non-zero values.

**Boot verification:** stderr now shows
`tables: data/buysell.txt — 504 bytes (debug=0 kyaku=14 kind=2)`,
matching the vendor file's expected values (debug commented, customer
14, kind "about"=2). All other 16 stubs continue to log size lines as
before; tutorial loop still stops correctly at `tuto4.txt`.

**Test status:** 37 tests pass (up from 29), no fails, no skips.

## 2026-05-20 — FUN_00475270 ("init indexfile ok") skeleton + Phase A discovery

**Subsystems landed:**
- `docs/findings/tables-loader.md` — discovery doc for the gameplay
  tables loader: caller context (it's the boot trace step right after
  `init render ok`), full file list with sizes and per-block C-line
  ranges, helper identities (storage_get_size / storage_read / atoi /
  atof / free), the two format families observed (`/key:value` for
  `config.idx`; CSV-with-comments for the `data/*.txt` files), and
  the proposed one-commit-per-file Phase B plan.
- `src/tables.{c,h}` — skeleton dispatcher `tables_load_all()` calling
  fourteen stub loaders (one per file) plus a tutorial-loop stub. Each
  stub exercises `storage_get_size` + `storage_read` end-to-end and
  logs the byte count to stderr; the real parsers will replace the
  printf in Phase B without touching the dispatcher.
- `src/main.c` — wired `tables_load_all()` into the boot chain at the
  TODO marker that was already pinned for `FUN_00475270`. Position
  matches the engine's `init render ok → [HERE] → init fontsys ok`
  ordering.

**Engine quirks documented this turn:**
1. `FUN_00475270` calls `storage_get_size` and `storage_read` with
   different `.data` addresses in every block — usually two interned
   copies of the same path string. For `config.idx` the developer
   accidentally typed two **different** spellings (get_size with
   `"config.idx"`, read with `"data/config.idx"`), so the original
   silently `malloc(0+10) = 10` and overruns by 940 bytes on every
   boot. Our stub uses the read-side spelling to avoid the bug.
2. Tutorial format string is `"data/tuto%d.txt"` (no underscore).

**Boot verification:** stderr trace from `openrecet.exe
--max-duration-ms 2000` shows all 17 storage reads succeed (14 fixed
+ 3 tutorials), and the loop correctly stops at `tuto4.txt`. Several
files come back larger than the lnkdatas size because they have a
bmpdata-overlay patched version (e.g. `enemy.txt`: 2801 → 3589).

**Test status:** 29 tests pass (no new tests yet — Phase B will add
per-file fixture tests as each parser lands). Boot smoke clean.

## 2026-05-20 — FUN_004341d4 bookkeeping (file-size helper)

Pinned candidate #2 closed as already-done. `FUN_004341d4` is the
trivial `fseek(0,SEEK_END); ftell; fseek(0,SEEK_SET)` file-size
helper, and it was already faithfully translated as
`storage_file_size` in `src/storage.c:139` during the
`storage_init`/`FUN_004341fe` port (with an in-file comment naming
the original). All four in-engine call sites we've ported (the ones
inside `storage_init` itself) route through it.

The other three inlined `fseek/ftell/rewind` idioms in `src/tga.c`,
`src/sprite.c`, and `src/lnkdatas_hash.c` were written by us, not
ports — they intentionally check the fseek/ftell return values
(the original doesn't). Left untouched so the defensive coverage
stays in place; promoting a 5-line static into a shared util module
would have been premature abstraction. Dropped from the
session-starter pin list.

## 2026-05-20 — lnkdatas content read + LZSS

**Subsystems landed:**
- `src/lnk_lzss.{c,h}` — port of FUN_004349e5 (the lnkdatas LZSS decoder).
  Pure C, no Win32 surface.  ~50 LOC. Stream is self-delimiting via the
  back==0 sentinel, so no input size is required.
- `src/storage.c` — extended `storage_get_size` and `storage_read` to fall
  back to the lnkdatas index when the asset isn't in the bmpdata overlay.
  Adds a 1-deep `bin/data%03d.bin` FILE* cache and a 10 MiB chunk-spanning
  reader (handles entries that straddle a `bin/data*.bin` boundary).
  Skips the original engine's 3× `Sleep(500ms)` retry loop around the
  fopen — that was robustness against transient I/O on 2007 spinning
  drives, not load-bearing for a modern Steam install.
- `tests/test_lnk_lzss.c` — 7 synthetic unit tests covering single
  literals, short / extended back-references, self-overlap RLE, the
  end-of-stream sentinel mid-control-byte, high-bit back-distances, and
  mixed flags within one control byte. Plus a vendor round-trip that
  iterates every entry in `vendor/original/lnkdatas.bin`, reads its
  slice (across chunk boundaries as needed), decompresses, and verifies
  the result length matches the declared `dsize` + that a one-byte
  output canary is intact.

**Two case-sensitivity quirks worth knowing:** the bmpdata branch of
`FUN_00434585` / `FUN_004346bf` does case-insensitive name matching
(A..Z folded to a..z) over 88 bytes; the **lnkdatas branch does a
straight byte compare** over 128 bytes — no fold. Our port mirrors
both. Callers relying on case-insensitive lookup must hit through the
bmpdata path.

**Pixel-exact validation:** rebuilt the standalone harness
`/tmp/storage_extract.exe` (built from `src/storage.c` with
`-DSTORAGE_TEST_EXTRACT`) and confirmed byte-identical output vs the
Python reference (`tools/extract/data-bin.py`) on 5 entries including
4 chunk-straddling ones (`xfile/koku_last/mahoujin.tga`,
`xfile/wall/kabe_check.bmp`, `bmp/chr/chr31.bmp`,
`bmp/worldmap_yugata.bmp`). Hashes match (SHA-256).

**Test status:** 29 tests pass (up from 21), no fails, no skips.
Sanitizer-clean. ASan caught two bugs while writing tests — both in
the *test fixtures*, not in the decoder: a mis-computed control byte
in `test_lnk_lzss_self_overlap` (0x28 should have been 0x30) and a
use-after-free on the canary value in the vendor round-trip. Good
ASan-pays-for-itself moment.

**Engine smoke:** boot scenario `tools/smoke-test.py` still exits 0
in ~4s on the rebuilt exe, debug-magenta clear color unchanged.

**Next pin (per session-start):** `FUN_00475270` is the big one —
3965 decompiled lines of `data/*.txt` parsing (item / kyaku / chara
/ enemy gameplay tables) plus `idx/stage.idx` and `idx/config.idx`.
Will likely need splitting across multiple commits.

## 2026-05-20 — Sanitizer-instrumented unit tests

**Subsystems landed:**
- `tests/Makefile` — Linux-native test harness. Host gcc +
  `-fsanitize=address,undefined -fno-sanitize-recover=all`. Run with
  `make -C tests run`.
- `tests/t.h` — dependency-free assertion macros (T_ASSERT, T_FAIL,
  T_SKIP) + UBSan-safe byte writers.
- `tests/test_main.c` — runs registered tests, supports name-substring
  filter via `argv[1]`, exits non-zero on any failure (skips don't
  count).
- `tests/test_{bmp,tga,bmp_lzw,lnkdatas_hash}.c` — 21 tests covering
  every audited code path in the four portable decoders.

**Why now:** the Win32 sprite loader just landed, and every decoder
ingests user-controlled bytes (BMP/TGA from `bmpdata.bin`, LZW slices,
CRC over the whole lnkdatas blob). Memory bugs in these can pass
pixel-equality checks while still being broken. Valgrind/ASan can't
run Win32 PE binaries, so the natural split is "Linux test target for
the portable .c files". The Win32 layer stays exercised by smoke
tests.

**Doc fix discovered during test writing:** `src/lnkdatas_hash.{c,h}`
called the engine's hash "CRC-16/CCITT-FALSE". It's *shaped* like
CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, final invert) but
the feedback step uses **subtraction** instead of XOR. The standard
CCITT-FALSE check value for "123456789" is 0x29B1; ours is 0xF5B7
because of borrow propagation. Cross-checked against
`/opt/src/recettear-repacker/crc.py`.

**Sanity check the harness catches real bugs:** temporarily injected
a 1-byte OOB read past the BMP pixel buffer and reran. ASan
pinpointed the exact `bmp.c:77` line with the heap region details.
Reverted; all 21 tests pass green.

## 2026-05-20 — `FUN_0047193c` ported — engine-style sprite loader

**Subsystems landed:**
- `src/bmp.{c,h}` — 24-/32-bit BI_RGB DIB decoder with color-key
  application (engine passes `0xFF00FF00` to D3DX → we test exact-match
  pure-green and zero the alpha). Top-down + bottom-up.
- `src/tga.c` extended — now also handles Type 10 RLE, plus an
  `tga_load_mem(buf,size,*img)` variant so the loader can decode
  storage-fetched bytes without round-tripping through disk.
- `src/sprite.c` — new `sprite_load(dev, name, w, h, *out)` entry
  point. Mirrors `FUN_0047193c`: tries `fopen(name,"rb")` first, falls
  back to `storage_read(name)`, sniffs `'BM'` → BMP-with-key vs TGA,
  decodes, uploads via the existing `sprite_create`.
- `src/main.c` — replaced `--show-tga <path>` (direct-file shim) with
  `--show-sprite <name>` that routes through the new `sprite_load`.

**Identification fix carried into `docs/findings/texture-loader.md`:**
the earlier write-up had the disk and storage calls swapped (claimed
the engine tried storage first with disk as fallback). Re-reading
`FUN_005038b0` as a thin `fopen` wrapper (forwards to `FUN_00503890`
with a `0x40` buffer-size hint) flipped it — **disk is tried first,
storage second**. The user-facing implication: external/mod overrides
on disk take precedence over the packed asset, consistent with how
`recettear-repacker` works.

**Validation (pixel-perfect, max diff 0/255 in all four cases):**
- Storage path: `--show-sprite bmp/ivent/ed_kasi11.tga` (resolves via
  `storage_read` from `bmpdata.bin`) renders byte-identical to a
  reference Python decode composited over the debug-magenta clear
  color, across all 512×32 pixels.
- Synthetic disk fixtures (built in-place, then cleaned up): a 64×64
  Type-2 TGA, a 64×64 Type-10 RLE TGA, and a 64×64 24-bit BMP with one
  half pure-green keyed and the other half opaque blue. All three
  round-trip 0 mismatches against the expected composite.

**Not yet engine-accurate:** D3DX-style resampling to `(expected_w,
expected_h)` is still skipped. Every audited asset ships at native
resolution, so this matters mainly for forward-compat / mod paths
that intentionally scale.

**Lifecycle fix (orphan-window cleanup, follow-up commit):** ad-hoc
`timeout 3 openrecet.exe …` runs kept leaving the host's Windows
side with orphan windows because `g_paused` blocks the main loop in
`WaitMessage` when the window loses focus — any deadline check
inside `tick_and_present` is never reached. Added `--max-duration-ms
<ms>` (also taken up by `tools/smoke-test.py`) that registers
`SetTimer` → `WM_TIMER` → `DestroyWindow`, which fires regardless of
pause state. Smoke harness now reaches `exit=0` gracefully instead
of falling through to SIGTERM/taskkill.

Next-milestone candidates: lnkdatas content-read path (so `storage_read`
also services `bin/data_NNN.bin` + the LZSS decompressor at
`FUN_004349e5`); `FUN_004341d4` standalone port (mostly mechanical);
diving into `FUN_00475270` (gameplay-text-table parser, 3965 lines —
needs splitting across commits).

## 2026-05-20 — `bmpdata.bin` LZW decoder + storage_read overlay path

**Subsystems landed:**
- `src/bmp_lzw.{c,h}` — 12-bit MSB-first LZW decompressor. Translation
  of `FUN_00434b32` (main loop), `FUN_00434c2c` (bit reader), and
  `FUN_00434ca9` (dict-chain walker). Dictionary frozen at 3839 entries,
  matches `recettear-repacker/bmp_unpack.py` exactly.
- `src/storage.{c,h}` extended — now also opens `bmpdata.bin`, slurps
  it into memory, validates the hash sentinel `0x21dc`, and exposes
  `storage_get_size(name)` + `storage_read(name, dst)`. Mirrors
  `FUN_00434585` (size lookup) and `FUN_004346bf` (read into buffer) for
  the bmpdata branch.

**Identification fix:** `FUN_00475270` (originally pinned as "likely
bmpdata.bin LZW loader" in PROGRESS) turned out to be the global
gameplay-text-table loader — a 3965-line parser for `data/item.txt`,
`data/chara.txt`, the `idx/stage.idx` chain, etc. The real LZW lives in
the much smaller `FUN_00434b32` + helpers, called lazily from the
storage read path. Plan annotation corrected.

**Engine deviation, by design:** the engine's `FUN_00434b32` doesn't
handle code 256 (LZW reset/EOS) — it walks past the dict base on the
sentinel and emits a few garbage bytes past the caller's `dsize`
buffer. Benign in the shipping game (callers tolerate the overrun) but
we'd rather not write past the asked-for size, so our decoder honors
256 explicitly. End result: byte-for-byte identical with
`bmp_unpack.py` output, not byte-for-byte identical with the engine's
overrun-prone output.

**Validation:** all 22 entries in the shipping Steam `bmpdata.bin`
round-trip through `storage_init → storage_read → stdout` to
byte-equal output vs the Python reference (see `/tmp/storage_diff.py`).
Boot smoke (`tools/smoke-test.py`) still green — exit signal, 3 frames
captured, no early-exit error from the now-stricter `storage_init`
(which is required to find `bmpdata.bin`).

**New format spec:** `docs/formats/bmpdata.md` — 84-byte names, 3 ×
int32 (dsize/offset/csize) per entry, 96-byte stride, 12-bit LZW
payload, hash sentinel `0x21dc`.

Next-milestone candidates (unchanged): `FUN_0047193c` (proper sprite
loader using `storage_*` with BMP + green-key + RLE-TGA — now possible
because `bmpdata` lookups work), `FUN_004341d4` standalone port, or
diving into the `FUN_00475270` gameplay-data parser.

## 2026-05-19 — Render-layer init ported (`FUN_00454e69` + `FUN_004038e4`)

**Subsystem landed:** `src/layers.{c,h}`. The "init render ok" hand-off
isn't device creation (that's step 11) — it's the engine fanning
`GetDeviceCaps` + back-buffer-desc + the live device pointer out into
its 24 per-layer state objects (each 0x2f0 bytes). Two arrays:
`g_layers_b[20]` (loop, `DAT_073da2f0` stride 0x2f0) and `g_layers_a[4]`
(unrolled in asm at `DAT_073cba20`/`+0x2f0`×3). See
`docs/findings/winmain-and-bootstrap.md` §"Render-layer init" for the
RE writeup + offset table.

**Layout corrections from the earlier guess:**
- The previous notes claimed the loop "zeros" the structs via
  `FUN_004038e4`. It doesn't — it actively writes `device` (`+0x108`),
  the back-buffer `D3DSURFACE_DESC` (`+0x10c`, 32 bytes), and a copy of
  `D3DCAPS8` (`+0x12c`, 212 bytes), then nulls `+0x200`.
- The 20-element loop is only *one* of two arrays; the 4 unrolled
  trailing calls operate on a *separate* 4-element array — easy to miss
  from the decompiler output because Ghidra strips the ECX setup before
  each thiscall.

**Skeleton wiring (`main.c`):**
- Removed the placeholder `IDirect3D8_GetDeviceCaps` from `init_render`
  — the real owner is now `layers_init`.
- `layers_init(g_d3d, g_dev)` slotted in after `input_init`, matching
  the original's `…dinput ok → init render ok` ordering (the previous
  comment had this misplaced).
- Bootstrap-order comments now mirror the actual call sequence.

**Why the struct is field-by-field (not a byte blob):** mingw's `d3d8.h`
ships `D3DCAPS8 = 212`/`D3DSURFACE_DESC = 32` — exact match to the
original's `rep movsl 0x35` and `0x12c−0x10c = 0x20`. Five
`_Static_assert`s on the known offsets + total size catch any future
header drift at build time.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--duration 4 --capture` — debug magenta `(160, 32, 96)` reads flat
across all 4 captured frames; no crash on init or shutdown.

Next-milestone candidates (unchanged): `FUN_00475270` ("init indexfile
ok" — likely `bmpdata.bin` LZW loader, cross-ref
`/opt/src/recettear-repacker/bmp_unpack.py`), `FUN_004341d4` (file-size
helper, quick mechanical port), or porting `FUN_0047193c` properly to
read assets via `storage_*` with BMP+green-key + RLE-TGA support.

## 2026-05-19 — Project bootstrap

**What landed**
- Decisions (see [`PLAN.md`](PLAN.md) §3): C + mingw-w64 32-bit, DirectX
  direct, MIT, Win32-first drop-in.
- `flake.nix` with full RE toolchain (ghidra 12, radare2, rizin, cutter,
  retdec, imhex, wine staging, frida-tools, mingw32 i686 cross compiler,
  python env with construct/scikit-image/pillow/opencv, xvfb-run, scrot,
  ffmpeg, imagemagick, pandoc).
- Directory structure: `src/`, `tests/`, `tools/`, `docs/`, `vendor/` (gi),
  `ghidra/` (gi).
- Plan, README, MIT license, `.gitignore` that aggressively protects any
  derived game data, `.editorconfig`.

**What we know about the original**
- `recettear.exe`: 32-bit PE, **SteamStub-packed** (VLV signature @0x80),
  5.6 MB on disk.
- `custom.exe`: ~462 KB config tool reading `recet.ini`.
- Assets: `bin/data###.bin` (custom archives, format TBD), `xfile/*.x` and
  `xfile2/*.x` (DirectX retained-mode `.x` text models — open spec),
  `bgm/*.wav`, `ef/effect*.dat`, `bmpdata.bin`, `lnkdatas.bin`,
  `recet_op.wmv`.
- `recet.ini` exposes: `winmode`, `fps`, `dispfps`, `usefog`, `usemipmap`,
  `usetree`, `windowpos`, `uselighttex`, `nolight`, `easydisp`, `bgnodisp`,
  `texlevel`, `toorioff`, `s_easydisp`, `sfnouse`, `pfnouse`,
  `fontmode1`/`fontmode2`, `screen`, `texmode`, `mapmode`, `demomode`, plus
  `pad##` / `skill##` key bindings and `[config] se`/`mu` audio levels.
  These are the engine's main feature toggles — each one is a hint about
  a code path we'll meet.

**Note on wine vs WSLInterop** — WSL has `WSLInterop` registered as a
binfmt handler, so `.exe` invocations run natively on Windows by default.
We use that for Steamless and for casual play. The automated test harness
still uses wine + Xvfb so that (a) the runtime is pinnable via the flake
and (b) original-vs-ours diffs share the same backend and don't surface
wine-vs-Windows differences as phantom bugs. See `PLAN.md` §6 for details.

**Tooling landed**
- `tools/setup.sh` — symlinks game, runs Steamless via WSLInterop, prints sha256s.
- `tools/ghidra-headless.sh` + `tools/ghidra-scripts/ExportDecompiledC.py` —
  batch decompile every function to `docs/decompiled/` (gitignored).
- `tools/smoke-test.py` — Xvfb+wine runner with frame capture and SSIM diff
  against a golden run.
- `tools/contact-sheet.py` — single-set or side-by-side downscaled grids, with
  optional `--zoom` full-res crop strip.
- `tools/extract/xfile.py` — DirectX `.x` text-format summarizer + tree scanner.

**First extractor result (validates pipeline)** — running
`xfile.py xfile/city/dun_city00.x` on the user's Steam install reports:

| template               | count |
|------------------------|------:|
| Material               |    58 |
| TextureFilename        |    25 |
| Frame                  |    13 |
| FrameTransformMatrix   |    13 |
| Mesh                   |    12 |
| MeshMaterialList       |    12 |
| MeshNormals            |    12 |
| MeshTextureCoords      |    12 |
| MeshVertexColors       |     1 |

All **standard DirectX 9 retained-mode** templates. No custom extensions →
`xfile/` and `xfile2/` can be parsed with stock `D3DXLoadMeshFromX*` or any
open-spec parser. (Phase 2 will scan the full tree for a definitive answer.)

**Flake quirk** — `retdec` currently fails to build in nixpkgs (capstone
sub-build error). Disabled it. Ghidra + radare2/rizin cover decompilation
cross-checks. Re-enable if upstream fixes.

**Next** (Phase 1 entry)
- User runs `./tools/setup.sh` to unpack the exe.
- Then `./tools/ghidra-headless.sh` to produce `docs/decompiled/`.
- First subsystem to map: WinMain + main loop. Find `D3D*Create*` calls in
  the decompiled output → confirms DirectX version (likely 8 or 9).

---

## 2026-05-19 — Setup ran, first findings from unpacked binary

- ✅ `tools/setup.sh` ran successfully on the user's machine — Steamless via
  WSLInterop produced `vendor/unpacked/recettear.unpacked.exe` (5.0 MB,
  down from 5.6 MB packed; 7 PE sections → 6 sections; VLV signature gone).
- Fixed `tools/ghidra-headless.sh`: nixpkgs ghidra names its binaries
  `ghidra-<tool>` (e.g., `ghidra-analyzeHeadless`), not bare `analyzeHeadless`.

**New findings — recorded in [`findings/imports-and-layout.md`](findings/imports-and-layout.md):**

- **DirectX version is 8** (d3d8.dll / d3d8d.dll / D3DERR_*). Fixed-function
  pipeline only — no shaders, no HLSL compiler required.
- **DirectX is loaded dynamically** via `LoadLibraryA` + `GetProcAddress` —
  static imports are only `KERNEL32`, `USER32`, `SHELL32`, `WINMM`,
  `ole32`, `ADVAPI32`. Six DLLs total. Very tight.
- **`DirectXFileCreate` is used** for `.x` model parsing (open DX File API).
- **No `dsound.dll` / `dinput.dll` static imports.** Audio likely via
  `WINMM` (`mciSendString` / `waveOut*`) or dynamically-loaded DSOUND;
  input likely raw `USER32` `WM_KEYDOWN` / `GetKeyState`. To be confirmed.
- **Asset layout discovered from strings:** the binary references
  `bmp/item/item%02d.bmp`, `bmp/item_win.tga`, `data/item.txt`,
  `bin/se/.../*.bin`, etc. None of these paths exist on disk — confirming
  that `bin/data###.bin` archives contain the `bmp/` (TGA/BMP textures)
  and `data/` (plain text gameplay tables) trees. Cracking this format
  unlocks all 2D art and all gameplay data.
- **All UI/dialogue strings are inline in `.rdata`** — no string table,
  no `.po` files. i18n story is "rebuild the binary".

**Next**
- ~~Run `./tools/ghidra-headless.sh`~~ Done — 2620 functions decompiled.
- ~~Locate the function that opens `bin/data000.bin` → archive format~~
  Pre-empted: spec was already cracked by UnrealPowerz/recettear-repacker.
- Locate `WinMain` and the `LoadLibraryA("d3d8.dll")` site → document
  the window+device init sequence for the skeleton in phase 3.

---

## 2026-05-19 — Ghidra working, cross-references absorbed

**Ghidra:**
- Fixed the post-script: nixpkgs Ghidra 12 isn't built with PyGhidra, so
  `.py` scripts fail. Rewrote `ExportDecompiledC` in **Java** — works
  in plain headless mode without flags.
- Also fixed a latent bug: the script had `-deleteProject` set on the
  first import, which would have wiped analysis state. Removed.
- Result: **2620 functions** decompiled into `docs/decompiled/all.c`
  (6.3 MB), `by-address/*.c`, `by-name/*.c`, `functions.csv`.

**Cross-reference projects** (cloned to `/opt/src/`):
- **UnrealPowerz/recettear-repacker** — full spec for `bin/data*.bin`
  archives. Format: 10 MiB chunks of LZSS-compressed blobs indexed by
  big-endian `lnkdatas.bin`. Custom LZSS variant with 12-bit back-distance
  + MSB-first ctrl byte. `bmpdata.bin` is a separate LZW-compressed
  update overlay.
- **ribeena/RecettearXTools** — `.x` ↔ USD Blender 4.1 converter; useful
  for double-checking our `.x` parser.
- **just-harry/FancyScreenPatchForRecettear** — runtime widescreen
  patcher; useful as a map of engine offsets we'll want to understand.

**New format spec:** [`formats/data-bin.md`](formats/data-bin.md).

**Our own extractor:** `tools/extract/data-bin.py` — clean Python
reimplementation matching the spec. Validated against upstream:
**byte-identical** output on the current Steam build (1188 files extracted).
Run `./tools/extract/data-bin.py vendor/original --validate-against
/opt/src/recettear-repacker` to re-verify.

**Newly confirmed about the engine:**
- **DirectInput 8** (`dinput8.dll`) is also dynamically loaded — found
  `DirectInput8Create` symbol at `0x4a1cc0`.
- **C++ compiled with MSVC** — `vector_constructor_iterator` /
  `vector_deleting_destructor` indicate array new/delete scaffolding.
- **MFC is statically linked** — `RFX_Text_Bulk` (MFC ODBC field exchange)
  present. Probably leakage from `custom.exe` sharing libs; possibly
  engine uses some MFC for save serialization (TBD).
- PE entry is at `0x5046c7` — MSVC `__tmainCRTStartup`. `WinMain`
  symbolic name not yet auto-resolved.

**Next investigation targets**
1. Trace `__tmainCRTStartup` → `WinMain`. Rename in the Ghidra project.
2. From `WinMain`, find the `LoadLibraryA("d3d8.dll")` call → document the
   DX8 device-creation sequence. (Skeleton for phase 3.)
3. Read `recettear-repacker/crc.py` — the engine probably uses the same
   CRC as a path hash for `bmpdata.bin` lookups. Worth porting.
4. Optional: read `FancyScreenPatchForRecettear` patch sites to find
   resolution-clamping code (a likely candidate for an early test
   subsystem since the patches are small and well-isolated).

---

## 2026-05-19 — Engine bootstrap mapped end-to-end

Full writeup in [`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md).
Highlights:

- **WinMain at `0x47bfb3`**. Identified by the standard MSVC
  `__tmainCRTStartup(hInst=GetModuleHandleA(NULL), 0, lpCmdLine, nCmdShow)`
  call signature in the PE entry.
- **Engine internal name is "Azumanga"** — that's EGS's name for their
  custom engine (also powers Chantelise). Window class is literally
  `"Azumanga Main Window"`.
- **Window title is `"RECETTEAR Ver 1.108"`** — exact version string for
  drop-in compatibility.
- **Debug logger `FUN_0047aa31` is a 1-byte `return;` stub** — all
  logging compiled out in the release build. The `s_init_*` string
  constants remain in `.rdata` as breadcrumbs, which **gave us the full
  subsystem init order for free**:
  `start → strage → print → dinput → render → indexfile → fontsys →
  daoudio → fontsystem → systemtex → savefile → titletex → main loop`.
  (Note Japanese-English typos preserved: `strage`, `daoudio`.)
- **Main loop function: `FUN_0047be92`** — the game tick.
- **`Direct3DCreate8(0xDC)` at line 77975 of `all.c`** — `0xDC = 220 =
  D3D_SDK_VERSION`. Global `IDirect3D8 *` is `DAT_073dfcb8`.
- **DirectInput 8 init: `FUN_0047af52`** — keyboard + EnumDevices for
  joysticks, with axis range ±5000 and 100-unit deadzone.
- **Storage init: `FUN_004341fe`** — tries `lnkdata.bin` (JP name) first,
  falls back to `lnkdatas.bin` (EN name). Validates the index via
  `FUN_00474f14` which must return `-0x7456` (`0xFFFF8BAA`); this is the
  engine's integrity hash, almost certainly matches
  `recettear-repacker/crc.py`.
- **WndProc `FUN_0047b2e7`** handles `WM_CREATE`, `WM_DESTROY`,
  `WM_ACTIVATE` (pause + DI un/acquire), `WM_CLOSE` (confirm dialog in
  windowed), `WM_KEYDOWN` (ESC only — rest of input via DInput).

**Process docs added:** [`AGENT-WORKFLOW.md`](AGENT-WORKFLOW.md) — codifies
the Opus-orchestrator / Sonnet-subagent split + briefing template + stop
conditions. Read at the start of every new session.

**Stop point:** engine bootstrap is mapped. Logical next milestone:
either (a) write the phase-3 skeleton drop-in `src/main.c` matching the
init order, or (b) start translating the high-value individual functions
(`FUN_0047be92` game tick, `FUN_00474f14` integrity hash, the d3d8 wrapper
that calls `Direct3DCreate8`). User to choose.

---

## 2026-05-19 — Phase 3 skeleton drop-in runs

**`src/main.c` + `src/Makefile` written and building.** The skeleton
mirrors the bootstrap chain from
[`findings/winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md):
high-resolution timer setup → window class register (`"Azumanga Main
Window"`) → CreateWindowExA (`"RECETTEAR Ver 1.108"`) → LoadLibraryA
(`d3d8.dll` → `d3d8d.dll` fallback) → `Direct3DCreate8(D3D_SDK_VERSION)`
→ `IDirect3D8::CreateDevice` → message pump with `PeekMessage`/
`WaitMessage`/`tick_and_present`. Each subsystem in the original's init
order is a `TODO` comment naming the `FUN_XXX` we still need to
translate.

**Builds at 77 KB** via `i686-w64-mingw32-gcc` from inside `nix develop`.
Static libs: `-ld3d8 -ldinput8 -ldsound -lwinmm -lgdi32 -luser32
-lkernel32 -lole32 -ladvapi32 -lshell32`.

Tick path currently does `Clear → BeginScene → EndScene → Present` with
a distinctive **debug magenta** clear color (`160, 32, 96`) so a working
boot is visually obvious vs a black-screen failure.

**Test harness pivoted to WSLInterop** (see updated `PLAN.md` §6):

- Modern nixpkgs `wineWow64Packages.stagingFull` skips the 32-bit
  `syswow64/` layer → 32-bit binaries fail to load `kernel32.dll`.
- `wineWowPackages.stagingFull` (classic dual-arch) builds from source on
  every machine, slow.
- WSL2 + WSLInterop is rock solid and runs the exe natively on Windows.
  Trade-off: tests pop a window on the desktop. We'll work around this
  with self-emitting back-buffer captures inside the exe
  (`--capture-to <dir>`, not yet wired).
- Wine dropped from the flake entirely.

`tools/smoke-test.py` rewritten — no Xvfb, no wine, no scrot. Launches the
exe via WSLInterop, captures exit code + duration + stdout/stderr + sha256.
First run: `openrecet.exe` ran cleanly for 3 seconds, was killed by
timeout (exit code -15, SIGTERM), `taskkill /F /IM openrecet.exe` confirmed
clean shutdown.

**Next**
1. Wire `--capture-to <dir>` into `src/main.c` — save back-buffer as 32-bit
   BMP every N frames, into the harness's `runs/<scenario>/<id>/frames/`.
2. Translate `FUN_00474f14` (the lnkdatas integrity hash) to validate our
   `tools/extract/data-bin.py` matches the engine's expected sentinel.
3. Start filling in subsystem stubs — first target: `FUN_004341fe`
   (storage init / lnkdatas loader) so the skeleton actually opens the
   game's index file. Good Sonnet-subagent task.

---

## 2026-05-19 — All three subagent tasks landed; capture pipeline works end-to-end

**Subagent 2: CRC hash port** — `src/lnkdatas_hash.{c,h}` +
`tools/extract/lnkdatas_hash.py`. Algorithm identified as
**CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`, MSB-first, final
`~crc`). Validates byte-identical against `recettear-repacker/crc.py`;
on the real `lnkdatas.bin` (sha256 `6c5b93cf…`) returns `0x8BAA`
(= `-0x7456`, the engine's "valid" sentinel).

**Subagent 3: Storage init port** — `src/storage.{c,h}`. Caught a
critical detail the first writeup missed: **`FUN_004341fe` has two
distinct format paths**. The JP build's `lnkdata.bin` (singular) has a
5-byte header skipped + a `byte' = 0x01 - byte` payload transform +
sentinel `0xC5E1`. The EN build's `lnkdatas.bin` (plural) is raw +
sentinel `0x8BAA`. Both implemented. Findings doc
[`winmain-and-bootstrap.md` §"Storage init"](findings/winmain-and-bootstrap.md)
corrected.

**Subagent 1: Frame capture** — `src/main.c` gained `--capture-to <dir>`
+ `--capture-every N` CLI flags. Renders BMPs at intervals via
`GetBackBuffer → LockRect → fwrite`. Initially silent-failed; root cause
identified as needing `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` in the present
parameters AND capturing **before** `Present()` (we use
`D3DSWAPEFFECT_DISCARD` which makes post-Present back-buffer undefined).
Both fixed. Capture now runs at ~5000 FPS in the empty-tick state
(660 frames captured in 4 seconds of un-vsync'd rendering — capture
overhead is negligible).

**Subagent integration issues** worth noting for future use of the
AGENT-WORKFLOW pattern:
- The first attempt at subagent 1 (frame capture) failed because
  `isolation: worktree` requires an existing git commit — we have none
  yet. Re-ran without isolation; safe because the other two created only
  new files. **Action:** make an initial commit before relying on
  worktree isolation.
- Subagents 2 and 3 raced on the `lnkdatas_hash` signature: subagent 2
  used `(buf, size) → int16_t`, subagent 3 assumed `(size, buf) →
  uint16_t` and inlined a fallback impl in `storage.c`. Caused a
  duplicate-symbol link error. **Action:** when subagents share an
  interface, brief them with the exact signature, not "infer it".
- Subagent 3 caught the JP/EN dual-format detail in `FUN_004341fe` that
  the orchestrator (me, Opus) had missed in the initial writeup. Good
  outcome — second-pass careful reading by a fresh agent surfaced
  something a quick first read glossed over.

**End-to-end visual confirmation:**
- User reported seeing the debug-magenta window during a manual run.
- Captured BMP frame 60 center pixel = exactly `RGB(160, 32, 96)`.
- 4-tile contact sheet via `tools/contact-sheet.py` shows all magenta.
- Pipeline: mingw32 build → `openrecet.exe` (91 KB) → WSLInterop →
  Windows 32-bit process → `storage_init()` loads 1188 lnkdatas entries
  via the EN path → DX8 device with `LOCKABLE_BACKBUFFER` → tick loop
  → BMP captures → contact sheet → visual diff ready.

**Tooling fix:** `tools/contact-sheet.py` now sets
`ImageFile.LOAD_TRUNCATED_IMAGES = True` to bypass PIL's strictness on
32-bit BI_RGB BMPs with an X-padding byte. The BMPs are structurally
valid (file size, headers, pixel layout all correct — verified manually);
PIL treats the X byte as alpha and trips a bounds check. Not actually
truncated.

**Stop point.** Skeleton boots, has working frame capture, has real
lnkdatas integrity-validated load. Logical next milestones (pick one,
or parallelize via subagents per AGENT-WORKFLOW.md):

1. **Initial git commit** so future subagents can use `isolation: worktree`.
2. **Translate `FUN_004341d4`** — the file-size helper used by storage init
   (currently we reimplemented it inline; matching the original is cleaner).
3. **Translate `FUN_0047af52`** — the DirectInput8 init chain.
4. **Translate `FUN_00475270`** — the "init indexfile ok" subsystem, almost
   certainly the `bmpdata.bin` LZW loader (cross-reference with
   `/opt/src/recettear-repacker/bmp_unpack.py`).
5. **Translate `FUN_00454e69` + surroundings** — the D3D8 device creation
   site, so we can match the original's exact present parameters and
   render-state initial values (necessary for pixel-identical diffs once we
   have real rendering).
6. **First real rendering** — load a single TGA from the extracted assets
   and draw it via a screen-aligned quad. Confirms the texture pipeline
   before we tackle any of the engine's actual draw paths.
- Wire up `tools/ghidra-headless.sh` for batch decompilation.
- Confirm DirectX version from unpacked imports.
- First extractor: `xfile.py` (validate pipeline against known format).

---

## 2026-05-19 — First real rendering: TGA + screen-aligned quad

**What landed:** `src/tga.{c,h}` (uncompressed truecolor TGA Type 2, 24/32-bit,
bottom-up or top-down → BGRA), `src/sprite.{c,h}` (`IDirect3DTexture8` via
`CreateTexture(D3DPOOL_MANAGED) + LockRect + memcpy`, screen-aligned quad
via `D3DFVF_XYZRHW | DIFFUSE | TEX1` + `DrawPrimitiveUP`, with
`SRCALPHA/INVSRCALPHA` blending and the standard half-pixel offset).
Wired into `src/main.c` behind a `--show-tga <path>` CLI flag.

**Verification.** Ran `openrecet.exe --show-tga bmp/window.tga
--capture-to <dir> --capture-every-ms 500` for 4 seconds via WSLInterop;
captured 8 BMPs showing the 64×64 `window.tga` (a rounded UI button)
correctly alpha-blended over the debug-magenta clear. Math check on
frame 4: TGA pixel `(32,32) = (23,23,47, α=133)` blended over
`(160,32,96)` predicts `(88, 27, 70)`; captured pixel reads `(89, 27,
70)` — agrees to within rounding.

**Engine-accuracy gap recorded** in
[`findings/texture-loader.md`](findings/texture-loader.md). `FUN_0047193c`
(the original's loader) hands work to `D3DXCreateTextureFromFileInMemoryEx`
(identified by 15-arg call site + D3DXERR_INVALIDDATA error path). For
BMPs it applies the green color-key `0xFF00FF00`; TGAs use native alpha.
We deliberately bypass d3dx8 (not in nixpkgs, deprecated) and will grow
our own decoders to match the engine's output. Current `tga.c` handles
Type 2 only — BMP-with-color-key and RLE-TGA come next.

**Next milestones (unchanged from prior stop point except #6 done):**

1. Translate `FUN_004341d4` (file-size helper).
2. Translate `FUN_0047af52` (DInput8 init chain).
3. Translate `FUN_00475270` (`bmpdata.bin` LZW loader; cross-ref
   `recettear-repacker/bmp_unpack.py`).
4. Translate `FUN_00454e69` + neighbours (D3D8 device creation, for
   matching the original's present parameters and initial render states).
5. Port `FUN_0047193c` properly — read assets via `storage_*`, accept
   BMPs with the green color-key, add RLE-TGA. Replaces `--show-tga`'s
   direct-file path.
6. ~~First real rendering~~ — done (this entry).

---

## 2026-05-19 — DirectInput 8 init ported (keyboard + joysticks)

**Subsystem landed:** `src/input.{c,h}` — full port of `FUN_0047af52`
("init dinput ok") plus its cleanup at `FUN_0047b0ef` and the
WM_ACTIVATE Acquire/Unacquire dance. Wired into `main.c` after
`init_render` and into `WndProc:WM_ACTIVATE` so deactivation correctly
releases device focus (matches the original's behavior).

**Pieces traced and ported:**
- `FUN_0047af52` — outer init: `DirectInput8Create`, keyboard create +
  `SetDataFormat(c_dfDIKeyboard)` + `SetCooperativeLevel(FOREGROUND|NONEXCLUSIVE)`
  + `SetProperty(DIPROP_BUFFERSIZE = 100)` + `Acquire`; then
  `EnumDevices(DI8DEVCLASS_GAMECTRL, ATTACHEDONLY)` followed by per-joystick
  `SetProperty(DIPROP_AXISMODE = ABS)` + `DIPROP_BUFFERSIZE = 100` + `Acquire`.
- `LAB_0047b167` — joystick enumeration callback. Ghidra never decompiled
  this (came up as a label, not a function); read directly from objdump on
  `vendor/unpacked/recettear.unpacked.exe`. Calls
  `IDirectInput8::CreateDevice(lpddi->guidInstance, ...)` into a 4-slot
  array, then `GetCapabilities` as a liveness probe — failure releases the
  device and zeroes the slot. Caps the joystick count at 4
  (`cmp 4; setl` — explains the static `g_joys[INPUT_MAX_JOYS]` layout).
- `FUN_0047b1f2` — per-object enum callback for `IDirectInputDevice8::EnumObjects`
  with filter `DIDFT_AXIS|DIDFT_POV`. Sets each enumerated object's
  `DIPROP_RANGE` to ±1000 via `DIPH_BYID`. (Earlier writeup said ±5000 — that
  was wrong; bytes are `0xFFFFFC18` = −1000 and `0x03E8` = 1000.)
- `FUN_0047b0ef` — symmetric shutdown: Unacquire+Release for the keyboard,
  each joystick slot, then Release the `IDirectInput8` factory.

**Other corrections to the bootstrap findings:**
- Keyboard `SetProperty` is `DIPROP_BUFFERSIZE=100`, not "DIPROP_RANGE ±5000"
  as I'd transcribed initially. The ±5000 number was never in the binary.
- The `WM_ACTIVATE` decision uses both `LOWORD(wParam)` (active/inactive)
  and `HIWORD(wParam)` (minimized flag): paused = inactive OR minimized.

**Toolchain note:** had to add `-ldxguid` to `src/Makefile` so the linker
resolves `IID_IDirectInput8A`, `GUID_SysKeyboard`, and the data-format
GUIDs that `c_dfDIKeyboard` / `c_dfDIJoystick` reference internally.

**Verified:** `tools/smoke-test.py --target openrecet --scenario boot
--capture` runs cleanly for 5 frames — debug-magenta still reads
`(160, 32, 96)` flat across the back-buffer, no crash on init or
shutdown, no MessageBox.

Next-milestone candidates (unchanged ordering from the session-starter
memo): `FUN_004341d4` (file-size helper), `FUN_00475270` (bmpdata.bin
LZW loader), `FUN_00454e69` ("init render ok" — post-device render-state
init), or porting `FUN_0047193c` properly to read assets through
`storage_*` and accept BMP+green-key in addition to TGA.

## 2026-05-19 — D3D8 device creation properly identified + matched

**Correction:** the bootstrap doc previously labeled `FUN_0047ac6a` as
"second-stage init" and `FUN_00454e69` as "init render ok". After
reading the `WinMain` dispatch carefully, **`FUN_0047ac6a` is the actual
D3D8 device-creation function** (`Direct3DCreate8` + `CreateDevice`,
present-params, behavior-flag fallback) and `FUN_00454e69` is post-device
render-state init that runs the "init render ok" log on completion.

**Findings updated** in
[`winmain-and-bootstrap.md`](findings/winmain-and-bootstrap.md): full
present-params field map, the unusual fullscreen=COPY+VSYNC swap-effect
choice (vs. windowed=DISCARD), the CreateDevice behavior-flag fallback
chain `0x44 (HW+MT) → 0x80 (MIXED) → 0x20 (SW)`, and the
`[setup] screen` resolution-lookup table
(0=640×480, 1=800×600 default, 2=1024×768, 3+=1280×960).

**Skeleton updated** — `src/main.c init_render()` now mirrors the
present-params layout (windowed/fullscreen split, COPY+VSYNC for
fullscreen) and walks the same `0x44 → 0x80 → 0x20` BehaviorFlags
fallback. Deliberate deviations recorded in code comments:
`hDeviceWindow=hwnd` (original leaves NULL → focus-window fallback,
behaviorally equivalent), `Flags=LOCKABLE_BACKBUFFER` only when
`--capture-to` is set (capture-only toggle), and hardcoded 800×600 until
the `recet.ini` parser lands.

**Verified:** smoke test runs cleanly with the new HW+MT-first chain;
sprite blend pixel still reads `(89,27,70)` — no regression.

Next: port `FUN_0047af52` (DInput8) — next subsystem in the bootstrap
order, contained scope.
