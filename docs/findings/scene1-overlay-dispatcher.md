# scene1 — `FUN_00414ee2` 2D-overlay particle dispatcher (survey)

**Status:** survey — chip O.1 in the proposed ladder below.  No port yet.

`FUN_00414ee2(int param_1, int param_2)` is a 4006-byte per-frame walker
that draws one `(layer, mode)` slice of a 2D-overlay particle system.  It's
the "screen-space sparkle / smoke / hud-effect" renderer.

```c
FUN_00414ee2(int layer, int mode);  // param_1, param_2
```

It is referenced from several render paths (see "Call sites" below); the
two HOUSE-relevant ones today are:

* **`wide_followup` mid_block_1** (`FUN_004161c7` L141) — `(1, 0)`
* **`scene1_render_overlay`** (`FUN_00417504`) — 4 calls covering `(0..3, 1)`

Both have TODO comments referencing this function in the port today.
There are additional callers in the 30 KB `FUN_004176ff` (out of scope
for this survey).

## Top-level structure

```c
void FUN_00414ee2(int layer, int mode)
{
    // Outer loop iterates each loaded "texture group" (layer
    // = bound D3D texture).  Set by the GRP_02d_* table parser
    // at FUN_00475040 (~L73306); dormant in HOUSE today.
    for (int outer = 0; outer < DAT_0076b948; outer++) {
        // Inner loop scans the fixed 4096-slot record table.
        for (int slot = 0; slot < 0x1000; slot++) {
            // Gates: ACTIVE != -1, LAYER == layer, MODE == mode,
            //        TEXTURE_GROUP == outer, AGE >= 0.
            if (r->active == -1)           continue;
            if (r->layer   != layer)       continue;
            if (r->mode    != mode)        continue;
            if (g_shape_texture_slot[r->type] != outer) continue;
            if (r->age < 0)                continue;

            FUN_00415e90(g_layer_textures[outer]);   // sticky SetTexture

            // Alpha = fade-in (0..255 over fade_in_dur) - fade-out tail
            // Reads slot fields: age, fade_in_dur, fade_out_dur, life,
            //                    shape_mode, blend_mode_byte.
            float alpha = compute_alpha(r);
            if (alpha < 0) continue;

            // Per-record shape dispatch (10 cases): see "Shape modes"
            switch (r->type_shape) {
                case 8: case 9: case 10: draw_group_of_5(r); break;
                case 7:                  draw_trail(r);      break;
                case 1:                  draw_lookat(r);     break;
                case 0: case 5:          draw_plain(r);      break;
                case 2:                  draw_premat(r);     break;
                case 3:                  draw_rotxyz(r);     break;
                case 4:                  draw_roty_pio2(r);  break;
                case 6:                  draw_roty(r);       break;
            }
        }
    }
}
```

The outer count `DAT_0076b948` is the number of loaded texture groups
parsed from a `GRP_02d_*` block in a text table file by
`FUN_00475040` (`docs/decompiled/all.c` L73306).  It's BSS-zero in our
port → the outer loop never enters → the function is a no-op in HOUSE
today.

## Record table

`DAT_0064e820..DAT_0072a8a0` — 4096 slots × stride **0x37 dw = 220 B
per slot = 901,120 B total**.  Per-slot field layout (offsets are byte
offsets within a 220-byte slot; Ghidra's named globals are stride-aliased
references to slot 0):

| offset | dw | Ghidra global    | field             | notes |
|-------:|----|------------------|-------------------|-------|
|  +0x00 |  0 | `DAT_0064e820`   | `texture_type`    | shape-table index (→ `&DAT_00769750[i*8]`) |
|  +0x04 |  1 | `DAT_0064e824`   | `type_shape`      | 0..10 — selects draw path |
|  +0x08 |  2 | `DAT_0064e828`   | `pos.x`           | float |
|  +0x0c |  3 | `DAT_0064e82c`   | `pos.y`           | float |
|  +0x10 |  4 | `DAT_0064e830`   | `pos.z`           | float |
|  +0x2c | 11 | `DAT_0064e84c`   | `bend.x`          | look-at target offset (shape 1) |
|  +0x30 | 12 | `DAT_0064e850`   | `bend.y`          | |
|  +0x34 | 13 | `DAT_0064e854`   | `bend.z`          | |
|  +0x38 | 14 | `DAT_0064e858`   | `rot.x`           | radians, used by shapes 3/7 |
|  +0x3c | 15 | `DAT_0064e85c`   | `rot.y`           | shapes 3/6/7/8/9/10 |
|  +0x40 | 16 | `DAT_0064e860`   | `rot.z`           | shapes 3/7 |
|  +0x48 | 18 | `DAT_0064e868`   | `unk_48`          | mode-4 fade gate (float) |
|  +0x4c | 19 | `DAT_0064e86c`   | `fade_out_offset` | int |
|  +0x50 | 20 | `DAT_0064e870`   | `scale.x`         | float |
|  +0x58 | 22 | `DAT_0064e878`   | `blend_mix`       | float ∈ [0,1] — green/orange channel split |
|  +0x5c | 23 | `DAT_0064e87c`   | `scale.y_ratio`   | float, only used by shapes 8/9/10 |
|  +0x60 | 24 | `DAT_0064e880`   | `age_birth`       | int — set at spawn time from a counter |
|  +0x64 | 25 | `DAT_0064e884`   | `fade_in_dur`     | int — frames for alpha to ramp 0→255 |
|  +0x68 | 26 | `DAT_0064e888`   | `fade_out_dur`    | int |
|  +0x6c | 27 | `DAT_0064e88c`   | `shape_mode`      | extra flag bits (mode 4 = "skip fade-out") |
|  +0x70 | 28 | `DAT_0064e890`   | `active`          | -1 = empty, else record header |
|  +0x74 | 29 | `DAT_0064e894`   | `age`             | int, must be ≥ 0 to render |
|  +0x78 | 30 | `DAT_0064e898`   | `scale_base`      | float (multiplied with scale.x for size) |
|  +0x80 | 32 | `DAT_0064e8a0`   | `rng_seed`        | int — drives UV mod/div for animated frames |
|  +0x84 | 33 | `DAT_0064e8a4`   | `layer`           | dispatch key (==layer arg) |
|  +0x88 | 34 | `DAT_0064e8a8`   | `blend_mode_byte` | 0/1/2 — fade-color vs fade-alpha channel mask |
|  +0x8c | 35 | `DAT_0064e8ac`   | `mode`            | dispatch key (==mode arg) |

The remaining offsets (0x14..0x28, 0x44, 0x54, 0x7c, 0x90..0xdc) are
used by the integrator side (motion/decay/etc.), which is **NOT in this
function** — `FUN_00414ee2` is the renderer only.  The per-frame
integrator is a separate function (~address 0x4xxxxx, look for the
walker writing `DAT_0064e890`); not surveyed here.

## Per-shape texture / atlas tables

A 256-entry side-table at `&DAT_00769750..` stride 8 dw (32 B per entry)
keys per-shape texture binding and UV layout.  Indexed by
`r->texture_type` (slot[+0x00]):

| offset | dw | Ghidra global    | field          | notes |
|-------:|----|------------------|----------------|-------|
|  +0x00 |  0 | `DAT_00769750`   | `tex_group`    | outer-loop key (which group binds the texture) |
|  +0x04 |  1 | `DAT_00769754`   | `uv_origin.x`  | float — px offset in atlas |
|  +0x08 |  2 | `DAT_00769758`   | `uv_origin.y`  | float |
|  +0x0c |  3 | `DAT_0076975c`   | `uv_size.x`    | float — tile width px |
|  +0x10 |  4 | `DAT_00769760`   | `uv_size.y`    | float — tile height px |
|  +0x14 |  5 | `DAT_00769764`   | `frame_count`  | int — total animation frames |
|  +0x18 |  6 | `DAT_00769768`   | `frame_period` | int — ticks per frame |
|  +0x1c |  7 | `DAT_0076976c`   | `loop_mode`    | 1 = loop, else clamp to last frame |

Per-record texture pointer:

```c
g_layer_textures[outer]    // DAT_073cc780 + outer * 0x10
                           // 16-byte slot per texture (IDirect3DTexture8* + 3 dw)
```

The 8 system-asset texture slots loaded at boot (`FUN_0047b29e`
L71666-71672 — already wired by our `sysassets_load_all`) include
`bmp/effect.bmp`, `bmp/effect_shot.bmp`, `bmp/shade.bmp`, etc. — these
are candidates for the layer table.

## Shape modes (per-record draw paths)

`r->type_shape` (slot[+0x04]) drives a 10-case dispatch.  All paths emit
into a small set of static screen-space vbufs and DrawPrimitiveUP at FVF
0x142 (XYZ + DIFFUSE + UV, 24 B stride).  The caller is expected to have
set `SetVertexShader(0x142)` and `SetTransform(WORLD, ...)` is updated
per record.

### Shape 0 / 5 — plain T × S × DAT_0438cdf8

`world = mat_translation(pos) × mat_scale(s, s_y_ratio, s) × DAT_0438cdf8`
where `s = (1-blend_mix)*scale_base*255_norm*scale.x*0.003 / 0.5`.

Single quad (4 verts in strip = 2 tris).  vbuf at static
`&DAT_0076b75c..&DAT_0076b7bc` (96 B = 4 verts).  UV box: `(u_origin +
0.5, u_origin + uv_size - 0.5) / 256.0`; vertical flip if
`(slot.rng_seed & 1) != 0`.  Diffuse: `0xRRGGBB << 8 | RR` where RR =
clamped alpha as gray.  DAT_0438cdf8 is the same shared pre-matrix used
by wide_followup Pass C/D and shop_walker Pass G (PHC #16 stand-in).

### Shape 2 — T × S × DAT_0438cdf8 (no scale-mix split)

Same as shape 0/5 but scale = `scale_base * scale.x * 0.003`, single
channel.  Same single-quad vbuf.

### Shape 3 — T × S × RotZ(rot.z) × RotY(rot.y) × RotX(rot.x)

Composes a full per-record orientation chain before the world matrix.
Same single-quad vbuf.

### Shape 4 — T × S × RotY(π/2)

Hard-coded yaw of π/2.  Float literal `0x3fc90fdb = 1.5707963` confirms
quarter turn.

### Shape 6 — T × S × RotY(rot.y)

Yaw-only orientation.

### Shape 1 — look-at billboard (single quad)

```c
target = pos + bend         // 3 floats per axis
up     = camera_eye - pos   // _DAT_073de31c..324 ≡ g_scene1_camera_eye
world  = mat_lookat_rh(pos, target, up)   // FUN_004a3b52
world  = mat_inverse(world)               // thunk_FUN_004a2f35(_, 0, _)
world  = mat_scale(s, s, 2*s) × world
world  = mat_rotation_y(π/2) × world
```

Reuses the lookat+inverse pattern from Pass E fan-billboard (`mat4_inverse`
helper added in C8f.pass-e-fan).  Same single-quad vbuf.

### Shape 7 — multi-quad trail (8..0x28 quads)

Per-record quad count derived from age:
```c
n = age * 2;
if (n > 0x20) n = 0x20;
if (age > 0x18) n -= (age - 0x18) * 2;    // ramp back down past frame 24
if (n < 4) skip;
```
Per-quad UV stripe at `&DAT_0064d430 + i * 0xc * vert_count` (vbuf has
~0x28 quad slots).  Per-record full RotXYZ matrix.

Diffuse = grayscale alpha as 0xRRGGBB.  DrawPrimitiveUP at
`primCount = n - 2`, stride 0x18 (XYZ+DIFFUSE+UV).

### Shape 8 / 9 / 10 — group-of-5 dual-billboard

Per-record fixed 5-quad emit:
```c
s_h  = (1-blend_mix) * scale_base * scale.x * alpha * 0.588 * 0.02
s_v  = (blend_mix * scale_base * scale.x * alpha * 1.26 / 0.5) * scale.y_ratio * 0.015
world = mat_translation(pos) × mat_rot_x(rot.y) × mat_scale(s_h, s_v, s_h)
```

`shape 10` does a per-slot 5-stripe variant; `shape 8` / `shape 9` differ
in the `& 1`/`== 9` vbuf-stride increment for the second 5-quad copy.
vbuf at `&DAT_00648e14..&DAT_0064c514` (per-record 0x1e0 stride for the
5-quad pack; ~0x50 records = 80 simultaneous shape-8/9 records max).

## Spawner — `FUN_00414345` (1057 B, 0x414345)

```c
void FUN_00414345(int  template_owner,  // param_1 — written to slot[+0x90/+0x94]
                  float pos_x,           // param_2..4: world pos
                  float pos_y,
                  float pos_z,
                  int   template_id,     // param_5 — index into &DAT_00733884
                  float scale_base,      // param_6
                  int   override_dur,    // param_7 — 0 → use template default
                  int   override_rot_y,  // param_8 — shape 6 (rotY) only
                  int   shape_mode,      // param_9 — slot[+0x6c]
                  int   mode);           // param_10 — dispatch mode key
```

Walks the 4096-slot table for `active == -1`, claims it, and copies
template constants from `&DAT_00733884[template_id * 0x2b]` (stride 0x2b
dw = 172 B per template).  Per-template fields fill rot/scale/lifetime,
RNG init for shapes 3/5/7/8/9/10, and `param_8` override for shape 6's
rot.y.  Caller-provided pos becomes `slot[+0x08/0x0c/0x10]`.

The template count is governed by `DAT_00733888` etc. — exact count
unknown; loader unidentified.

## Globals inventory

### Per-instance state

| Address                       | Size      | Role                         | Loader / writer |
|-------------------------------|-----------|------------------------------|-----------------|
| `DAT_0064e820..0072a8a0`      | 0xdc * 4096 = 901 KB | 4096-slot record table | `FUN_00414345` (spawner) |
| `DAT_0076b948`                | 4 B       | Loaded layer count (0..256) | `FUN_00475040` (table parser) |
| `DAT_0076b94c`                | 4 B       | Loaded record-type count    | Same parser |
| `DAT_0076b95c`                | 4 B       | Sticky-bound texture ptr    | `FUN_00415e90` |
| `DAT_0076b950..b958` (3 dw)   | 12 B      | Per-frame anim counter for chr/HUD overlays | `FUN_00414c0c` (integrator?) |
| `DAT_073cc780..`              | 16 * N    | Per-layer D3D texture slots | `FUN_0047b29e` L71674-71682 (`sysassets_load_all` sibling) |
| `DAT_0072a820..72a8a0`        | 256 * N   | Per-layer texture filename (e.g. `bmp/groupname.bmp`) | `FUN_00475040` L73318 |
| `DAT_0076b75c..0076b7bc`      | 96 B      | Static single-quad vbuf     | Per-frame |
| `DAT_00648e14..0064c514`      | 0x1e0 × 0x50 = 60 KB | Group-of-5 vbuf | Per-frame |
| `DAT_0064d408..0064d430`      | 40 B + 0xc × N | Multi-quad trail vbuf  | Per-frame |

### Per-shape (texture/UV) state

| Address                       | Size      | Role                         | Loader / writer |
|-------------------------------|-----------|------------------------------|-----------------|
| `DAT_00769750..769770`        | 0x20 × N  | Per-shape tex_group + UV box + frame count | Table parser |

### Template (spawn-time) state

| Address                       | Size      | Role                         | Loader / writer |
|-------------------------------|-----------|------------------------------|-----------------|
| `DAT_00733884..`              | 0xac × N  | Per-template defaults (rot, scale, life, ...) | Table parser |

### Shared with other passes

| Address                       | Role                                       |
|-------------------------------|--------------------------------------------|
| `DAT_0438cdf8`                | Shared pre-matrix (PHC #16 RESOLVED 2026-05-24 chip O.11 — writer is `FUN_00452f58` via `mat4_mul` at 0x4530b8; under engine HOUSE state (eye=(0,0,0), lookat=(0,0,-550)) the value is RotationY(π/2); pre-O.11 our stand-in was identity) — used by shapes 0/2/5 |
| `_DAT_073de31c..324`          | Camera eye position (=`g_scene1_camera_eye`) — used by shape 1 lookat |
| `DAT_073dfcbc`                | `IDirect3DDevice8 *` (already wired) |

### Loaders (not yet ported)

* **`FUN_00475040`** (table-file parser, ~L73272+) — reads a text table file
  (likely `extra/scene1/effect.tbl` or similar) parsing:
  * `GRP_02d_<n>` blocks → fill `&DAT_0072a820 + n*0x100` filename + bump
    `DAT_0076b948`.  Each layer name = a "texture group" → atlas filename.
  * (Presumably) per-record `PRT_*` / `SET_*` blocks → fill `&DAT_00769750`
    and `&DAT_00733884`.  Format not yet decoded.
* **System asset hookup** at `FUN_0047b29e` L71674-71682 — iterates loaded
  layer filenames calling `FUN_0047193c(1, &DAT_073cc780[n], &DAT_0072a820[n*0x100], 0x100, 0x100)`.
  Wired today via `sysassets_load_all` but the post-`DAT_0076b948 != 0`
  loop has nothing to iterate.

## Math thunks (canonical mapping per math3d.h)

| Ghidra thunk            | Canonical |
|-------------------------|-----------|
| `thunk_FUN_004a3462`    | `mat4_translation(M, x, y, z)` |
| `thunk_FUN_004a33d2`    | `mat4_scaling(M, x, y, z)` |
| `thunk_FUN_004a35d3`    | `mat4_rotation_x(M, angle)` |
| `thunk_FUN_004a3537`    | `mat4_rotation_y(M, angle)` |
| `thunk_FUN_004a3670`    | `mat4_rotation_z(M, angle)` |
| `thunk_FUN_004a2a03`    | `mat4_multiply(out, left, right)` — left-mul in-place |
| `thunk_FUN_004a2f35`    | `mat4_inverse(out, mode, in)` — mode=0 selects affine inverse |
| `FUN_004a3b52`          | `mat4_lookat_rh(out, eye, target, up)` |

All already exist in `src/math3d.{c,h}`.

## Call sites

| Caller                                                  | Args        | Status |
|---------------------------------------------------------|-------------|--------|
| `FUN_004161c7` L141 (wide_followup mid_block_1)         | `(1, 0)`    | port has TODO stub |
| `FUN_00417504` L13912 (scene1_render_overlay)           | `(1, 1)`    | not yet ported |
| `FUN_00417504` L13915                                   | `(0, 1)`    | not yet ported |
| `FUN_00417504` L13919                                   | `(2, 1)`    | not yet ported |
| `FUN_00417504` L13921                                   | `(3, 1)`    | not yet ported |
| `FUN_004176ff` L14277  (30 KB Mt. Everest renderer)     | `(4, 0)`    | scope-deferred |
| `FUN_004176ff` L14291                                   | `(0, 0)`    | scope-deferred |
| `FUN_004176ff` L18873  @ 0x41e5c0                       | `(2, 0)`    | scope-deferred |
| `FUN_004176ff` L19139  @ 0x41ec08                       | `(0, 0)`    | scope-deferred |

## HOUSE dormancy

With `DAT_0076b948 == 0` (BSS-zero in our port), the outer loop never
enters → `FUN_00414ee2` is a no-op.  Visible behavior matches retail
exactly *as long as the table parser stays unported*.  This is the same
class of dormancy we have for the wide_followup walker passes:
structurally complete but starved of input data.

The function only fires once **either**:

1. The `FUN_00475040` parser ports (lands real `DAT_0076b948 > 0`), or
2. A `--force-overlay-spawn` smoke flag + a hand-built layer table is
   wired through `main.c` (analogous to the existing `--force-ambient-spawn`
   / `--force-b-*` / `--force-c-*` flag families).

## Proposed chip ladder

This survey is **chip O.1**.  Subsequent chips (each landable
independently, dormant until the populator lands or smoke flags arm them):

* **O.2 — Typed storage + spawn API** (~300 LoC).  `src/scene1_overlay.{c,h}`
  declares the 220-byte slot struct (4096 entries), 172-byte template
  struct (count unbounded), 32-byte per-shape struct (count unbounded).
  Ports `FUN_00414345` as `scene1_overlay_spawn(...)`.  Host tests cover
  template lookup + RNG-driven init for shapes 5/7/8/9/10 + shape-6 yaw
  override.  Templates and per-shape tables remain BSS-zero (no parser
  yet).  Spawn returns a free-slot index or -1; no rendering yet.

* **O.3 — Dispatcher shell + shapes 0/5** (~150 LoC).  `scene1_overlay_render(dev,
  layer, mode)` implements the outer-by-layer-texture × inner-4096
  scan + sticky SetTexture + alpha compute.  Shape 0/5 is the simplest
  draw path (single quad, T × S × DAT_0438cdf8).  Reuses
  `g_wf_pass_c_pre_matrix` getter for the shared pre-matrix.

* **O.4 — Shapes 2/3/4/6** (~120 LoC).  Matrix variants on the shape-0
  template:
  * **2** — same as 0/5, alternate scale
  * **3** — full RotZ × RotY × RotX before pre-matrix
  * **4** — RotY(π/2) only (literal)
  * **6** — RotY(rot.y) only

* **O.5 — Shape 1 look-at billboard** (~80 LoC).  Reuses
  `mat4_inverse` from Pass E fan-billboard (C8f.pass-e-fan).  Single
  quad, eye=g_scene1_camera_eye.

* **O.6 — Shape 7 multi-quad trail** (~150 LoC).  Variable-count quad
  strip; new vbuf (or shared static); age-driven count function.

* **O.7 — Shapes 8/9/10 group-of-5** (~200 LoC).  Per-record 5-quad
  dual-billboard; per-record 60 KB vbuf split across `DAT_00648e14..0064c514`.

* **O.8 — Wire dispatcher into mid_block_1** (~30 LoC).  Replace the
  TODO in `wide_followup mid_block_1` with `scene1_overlay_render(dev,
  1, 0)`.  Dormant until O.10 lands real records.

* **O.9 — Wire dispatcher into scene1_render_overlay** (~50 LoC).  Port
  `FUN_00417504` (506 B, C7h follow-up) as `src/scene1_overlay_render.c`
  with the 4-call shell (one per layer ∈ {0,1,2,3}, mode=1).

* **O.10 — Table-file parser** (TBD scope; not surveyed).  `FUN_00475040`
  decodes the `GRP_02d_*` text file into `DAT_0076b948 + DAT_0072a820 +
  per-shape DAT_00769750 + per-template DAT_00733884`.  Once this lands,
  the layer texture loader at `sysassets_load_all` (already wired via
  `FUN_0047193c` calls) populates the per-layer texture slots and the
  dispatcher starts producing visible particles for any spawn site that
  fires.

Chips O.2..O.7 can land independently (each is structurally complete
even with downstream chips deferred).  O.8/O.9 are wiring deltas.  O.10
is the populator that unlocks visible deliverables for all of them.

## Risks / hazards

* **Shape 7 quad-count math.**  The decomp shows `n = age * 2; clamp;
  ramp-down past frame 24` but the exact off-by-one of "quads vs
  vertices" needs raw-asm verification at the DrawPrimitiveUP call
  (`prim_count = n - 2`) — strip semantics require careful counting.
* **Shape 8/9/10 second-copy stride.**  Decomp uses `uVar8 & 1` to
  branch between 2 different 0x1e0-stride vbuf bases; need to confirm
  this is "type 8 → bank A, type 9 → bank B" vs some per-frame
  rotating-bank semantics.
* **`DAT_0076b95c` sticky-tex caching.**  Sibling functions (`FUN_00415e90`,
  Pass G in shop_walker, several other paths) all read+write the same
  global.  Concurrent dispatcher runs in the same frame (mid_block_1
  fires AND scene1_render_overlay fires) must respect cache invalidation
  at the boundaries — verify the order-of-passes resets the cache as
  expected (or doesn't — could be intentional cross-pass cache reuse).
* **Template/shape table count.**  No clear loop bound on `DAT_00733884`
  or `DAT_00769750` in the survey.  Likely 256 (byte index from
  template_id) but want a Frida read of the parsed count after a known
  table file loads.
* **Shape mode 8/9/10 BLEND_MODE_BYTE.**  The `cVar1` byte at `+0x88`
  drives 3 sub-modes (0/1/2) for color-vs-alpha-vs-both fade.  The
  exact composition (fade × color × alpha) needs visual smoke once
  templates exist.

## Survey provenance

* Decomp source: `docs/decompiled/by-address/414ee2.c` (4006 B, 517
  decomp lines).
* Spawner source: `docs/decompiled/by-address/414345.c` (1057 B, called
  via `FUN_00414345`).
* Texture-cache helper: `docs/decompiled/by-address/415e90.c` (36 B,
  sticky SetTexture wrapper).
* Table parser inferred from `docs/decompiled/all.c` L73306..L73326
  (`GRP_02d_*` substring match → `DAT_0076b948` increment).
* Per-layer texture loader at `docs/decompiled/all.c` L71674-71682
  (already partly wired by `sysassets_load_all`).
