# HOUSE render fidelity gaps (port vs retail)

Visual diffs between the port's HOUSE render and retail, to re-check as
more of the scene-1 pipeline lands. Baseline: PII.3c (shop interior
background — room + carpet via draw loop A) + the kabe/yuka/jutan
texture-hook wiring, 2026-05-29. Capture: `runs/house-bg-tex/frame_03300.png`
(port) vs retail.

User-reported diffs from the first full-room render (2026-05-29):

## RESOLVED

### Floor / walls / rug rendered as untextured "blurry grey"
- **Symptom:** wooden floor → blurry grey; smooth-grey walls (with
  cracks, e.g. bottom-left of frame) → grey; red rug under the centre
  table → grey. Geometry correct, texture missing.
- **Root cause:** draw loop A's per-cache-slot SetTexture dispatch
  (decomp L52813-L52870) binds the *per-stage* wall/floor/rug textures
  (kabe / yuka / jutan classes) rather than the mesh's embedded
  TextureFilename. `scene_walls/floor/jutan` already `sprite_load` the
  selector-matched `.bmp` into `g_scene_X[selector]`, but the
  `s_hook_{kabe,yuka,jutan}_tex` hooks were NULL in production (only
  ever set in tests) → `SetTexture(NULL)` → untextured.
- **Fix:** `scene1_preload_house` installs `house_{kabe,yuka,jutan}_texture`
  adapters after the foreground loads (commit 15db713). User-verified
  fixed.

## LANDED (2026-05-29, PII.3d) — per-stage maplight builder

`FUN_00458f67` (the per-stage FFP map-light builder, was stubbed as
`scene1_walk_pre_dispatch_TODO`) is ported as `src/scene1_maplight.{c,h}`
(`scene1_build_maplight` + `scene1_maplight_rebind`) and wired into
`scene1_render_meshes` at both light sites (L199 build, L220-230 rebind).

**Erratum corrected:** the earlier note that the HOUSE palette is
all-zero (lighting off) was wrong. The engine parses `stage.idx`
straight into the `DAT_068dd2f8` table that `DAT_068dd2f0` indexes, so
the live stage palette IS the parsed record. HOUSE (`stage:0-1`) is
`maplight:3` (time-of-day town light) with `lightdir/color/amb`,
`fog:20:500`, `fogcolor:230:240:255`, `hikaridrawcode:2`,
`hikarialpha:96`, `hikariadd:1`. The port's renderer accessors
(`scene1_palette_lighting_enabled`, `..._fog_start/end`,
`..._fog_color_*`) now read `g_stage.records[HOUSE]` via the new
`scene1_current_stage_record()` bridge instead of returning 0.

The `SetLight` args were NOT Ghidra-dropped — `FUN_00458f67` builds the
full `D3DLIGHT8` field-by-field at `DAT_06a49a40`. The mode-3 preset
table (`MAPLIGHT3_PRESET`, 3 time-of-day rows) is verified against the
decomp `local_98[0..0x1a]`. The day/night clock (`DAT_0438b1e0` /
`DAT_0450fb88` / `DAT_0438b7d4`) is unported, so mode 3 uses the
daytime row 0 (the fresh-HOUSE-entry default). Unit-tested in
`tests/test_scene1_maplight.c` (8 cases, all modes + chr-ambient clamp).

**Still open after this chip:** gap #1 (hikari god-rays) below — the
maplight prerequisite is now satisfied, but the hikari texture source +
additive blend are still unwired (`s_hook_animated_tex` NULL). Gap #2
(blinds on lit triangles) needs visual re-verification now that
`D3DRS_LIGHTING` is enabled with the real directional light — note the
visible delta also depends on the meshes carrying vertex normals + a
material, which is a separate path.

## DEFERRED — hikari god-ray overlay

### #1 — "frustum-ish" solid mesh in front of the window
- **Symptom:** a frustum-shaped solid mesh sits in front of the window;
  in retail this region is animated **god rays** (光 = *hikari*).
- **Hypothesis:** the god-ray geometry classifies as
  `SCENE1_WALKER_SLOT_HIKARI` (param_1==3 / "hikari" texture prefix),
  bound via `s_hook_animated_tex` — **also NULL in production** (not
  wired by the kabe/yuka/jutan fix). With no hikari texture + no
  additive blend, the ray geometry draws as an opaque solid frustum.
- **Needs:** (a) a hikari/animated texture source + the
  `animated_texture_hook` wired; (b) the additive-blend + light state
  the rays draw under. There is no `scene_hikari` loader yet — the
  hikari texture source is unidentified. Investigate the engine's
  hikari overlay pass (the `param_1==3` walker invocation) + its blend
  state before wiring.

### #2 — blinds texture mangled on lit triangles
- **Symptom:** the window-blinds texture is partially mangled, worse on
  the right; each lit triangle renders as almost a solid color, and the
  effect scales with god-ray intensity (only the *lit* parts are
  affected).
- **Hypothesis:** missing scene-1 per-vertex lighting. With
  `D3DRS_LIGHTING=FALSE` and no per-stage `SetLight`, the engine's
  smooth per-vertex light gradient collapses to flat per-face shading;
  combined with whatever the hikari/light contribution modulates onto
  those faces, the lit tris go solid-color. The "more god rays = more
  mangled, only lit parts" coupling points squarely at the light/
  hikari contribution, not the base texture (which is correct on the
  unlit parts).
- **Needs:** the same scene-1 lighting chip as #1 — port the per-stage
  maplight (`DAT_06a49a40` D3DLIGHT8 + the dropped `SetLight` args) and
  enable `D3DRS_LIGHTING`.

## Suggested next chip

**Scene-1 lighting + hikari god-ray overlay** — ports the per-stage
maplight (resolve the Ghidra-dropped `SetLight` args via objdump, like
the phase-1 column→axis remap) and the hikari overlay pass (texture
source + additive blend + `animated_texture_hook`). Closes #1 and #2
together. Ground-truth the light params + hikari texture via a Frida
read at the HOUSE frame (model on `tools/dump_phase1_groundtruth.py`).

## Other known-missing HOUSE elements (not user-reported, tracked elsewhere)
- 2D HUD overlay (FUN_0040a765 / C7i) — the shop UI/HUD layer.
- Cr.2 overlay re-enable (PHC #18, COLORARG2 leak).
