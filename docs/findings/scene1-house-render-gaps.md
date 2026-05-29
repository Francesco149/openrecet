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

## RESOLVED (2026-05-29, PII.3d.2 + .3) — hikari submesh texture + additive blend

Gaps #1 (opaque "frustum" in front of the window) and #2 (rainbow
solid-colour triangles on the window/blinds where lit) were the SAME
bug, exposed by enabling the maplight: the hikari-flagged submeshes of
`shop_1st.x` draw only in the alpha walker's pass 3 but bound a NULL
texture, so under `COLOROP=MODULATE2X` lighting made each face's vivid
placeholder material colour show as an opaque rainbow frustum.

- **.2** (`scene1_walker_pass_init.c`): the HIKARI/WATER pass now binds
  the cache slot's own sprite — which is `xfile/shop/hikari.bmp` (a
  64×64 soft blue-cyan light-shaft gradient, the embedded `hikari*`
  texture of `shop_1st.x`). Equivalent to the engine's single-frame
  `DAT_073aa198[0]` lookup. Also wired the `aw_palette_*` blend/combiner
  gates to the live stage record (`hikariadd=1`, `hikaridrawcode=2`,
  `hikarialpha=96`, `drawcode=2`).
- **.3** (`scene1_alpha_walker.c`): fixed the blend-constant erratum —
  the arms wrote `D3DBLEND_DESTCOLOR` (9) where the engine emits literal
  `2 = D3DBLEND_ONE`, turning the additive god-ray blend into a darkening
  multiply. Now additive (`src + dest`).

User-verified: artifacts gone, frustum gone, hikari layer adds a soft
glow. **Not retail-equivalent yet** — see below.

## DEFERRED — the bright "blinding" window ray-shaft layer

- **Symptom:** retail HOUSE (`/mnt/c/Users/headpats/Documents/house-3d.png`)
  has bright, blinding white-cyan ray shafts blooming out of the windows
  and a generally brighter scene. Our render has the correct base
  lighting + the soft hikari-submesh glow, but not the dramatic shafts.
- **Source identified:** `FUN_00459847(3)` — the alpha walker
  (`FUN_00458bdf` L53671) calls it right after the hikari submesh pass
  `FUN_00457714(3)`. It is **stubbed** in the port as
  `aw_narrow_frustum_walker_TODO` (`scene1_alpha_walker.c`). Decomp
  (0x459847, 1444 B): the `param_1==3` branch sets additive blend
  (SetRenderState SRCBLEND=ONE/DESTBLEND=ONE, L54028-54038) and draws
  from a record table `DAT_005c4cac` (stride 0x24) filtered to type 3.
  This is the additive ray-shaft / billboard renderer. (Called with
  0/1 from `scene1_render_meshes` and 2/3 from the alpha walker.)
- **Next chip:** port `FUN_00459847` (the additive billboard/ray walker).
  Ground-truth the intensity with a same-res retail HOUSE capture
  (Frida-remote harness) once the geometry lands.

## Suggested next chip (superseded — see above)

**Scene-1 lighting + hikari god-ray overlay** — ports the per-stage
maplight (resolve the Ghidra-dropped `SetLight` args via objdump, like
the phase-1 column→axis remap) and the hikari overlay pass (texture
source + additive blend + `animated_texture_hook`). Closes #1 and #2
together. Ground-truth the light params + hikari texture via a Frida
read at the HOUSE frame (model on `tools/dump_phase1_groundtruth.py`).

## Other known-missing HOUSE elements (not user-reported, tracked elsewhere)
- 2D HUD overlay (FUN_0040a765 / C7i) — the shop UI/HUD layer.
- Cr.2 overlay re-enable (PHC #18, COLORARG2 leak).
