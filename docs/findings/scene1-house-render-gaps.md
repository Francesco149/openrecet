# HOUSE render fidelity gaps (port vs retail)

Visual diffs between the port's HOUSE render and retail, to re-check as
more of the scene-1 pipeline lands. Baseline: PII.3c (shop interior
background — room + carpet via draw loop A) + the kabe/yuka/jutan
texture-hook wiring, 2026-05-29. Capture: `runs/house-bg-tex/frame_03300.png`
(port) vs retail.

User-reported diffs from the first full-room render (2026-05-29):

## OPEN HUNT LIST — HOUSE render discrepancies (post-brightness-fix, 2026-05-29)

User-reported after the MODULATE2X brightness fix (commit 8d4e376) landed
and matched retail 1:1 on base brightness. These are the remaining
HOUSE visual gaps to hunt down (most cluster in the hikari/window
god-ray subsystem, which is only partially ported):

1. **Blinds: mangled rainbow triangles on the windows.** RESOLVED
   (commits 463a810 + 960e4ee). Two Ghidra type-confusion bugs in the
   HOUSE-dormant scene1_wide_followup walker leaked wrong state into the
   later hikari/blind overlay draws:
   (a) COLORARG1=TEXTURE leak — wide_followup mis-ported engine
       MIN/MAGFILTER(0x11/0x10)=LINEAR as COLORARG2/COLORARG1=TEXTURE
       (value 2 collides between D3DTEXF_LINEAR and D3DTA_TEXTURE), so
       the overlay draws computed MODULATE2X(TEXTURE×TEXTURE)=texture².
   (b) DESTBLEND=INVSRCCOLOR leak — wide_followup mis-ported engine
       SetRenderState(SRCBLEND,5)+(DESTBLEND,6) (raw states 0x13/0x14)
       as SetTextureStageState(MAGFILTER,5)+(MINFILTER,6), so it never
       reset DESTBLEND from the earlier INVSRCCOLOR(4) to INVSRCALPHA(6).
       Under alpha-blend that gave the colour-keyed red/blue/cyan overlay.
   After both fixes the port's HOUSE draw *state* matches retail exactly:
   base 42×(MODULATE2X, opaque), overlay 8×(SRCALPHA/INVSRCALPHA,
   MODULATE2X, COLORARG1=DIFFUSE), additive 5×(ONE/ONE). Traces:
   runs/port-d3d-house5 vs runs/retail-d3d-house. **Lesson: wide_followup
   is dormant in HOUSE (draws nothing) but its render-state preamble runs
   and LEAKS into later passes — audit dormant walkers' state writes too.**

   **PARTIAL FIX — cyan→green (commit 4070c3f), but curtains still
   diverge.** The hikari pass (FUN_00457714 param_1==2/3) binds the
   engine's animated hikari texture DAT_073aa198[frame], NOT each hikari
   submesh's embedded sprite. The engine loads those frames in the HOUSE
   asset loader (FUN_00474a9a L73104-73113): `"<prefix><NN>.bmp"` where
   the HOUSE prefix is `mood_para`, count = `*(int*)(&DAT_068ded18 +
   stage*0x1b3c)`, via the "%s%02d.bmp" format (s__s_02d_bmp_005ca61c).
   The port had loaded `mood_para` but mislabeled it "UI scratch" and
   left s_hook_animated_tex NULL → the pass fell back to the submesh's
   cyan embedded sprite. Wiring house_hikari_texture()→g_scene1_mood_para
   turned the curtains GREEN.

   **RESOLVED (2026-05-29 PM, commits ec573a9 + 4a88b5a; user-confirmed
   "lighting looks basically perfect now").** Two stacked root causes,
   both pinned by a D3D state-trace A/B (runs/port-d3d-house9 frame 3300
   vs runs/retail-d3d-house frame 14000) on the 5 hikari god-ray
   submeshes of shop_1st.x (start_idx 4497/4521/4761/4833/5919, prim
   8/8/24/24/16 — byte-identical draw set on both sides):

   - **(a) wrong TEXTURE.** Retail binds `SetTexture(0, NULL)` for the
     hikari draws — HOUSE's `DAT_073aa198[]` frame table is EMPTY, so the
     glow is pure vertex-diffuse ×2 additive. The mood_para binding
     (4070c3f) painted a flat-green texture where retail uses none. The
     "HOUSE hikari prefix = mood_para" premise was wrong. Fix:
     `house_hikari_texture()` returns NULL. → curtains became translucent
     additive glow (right method) but CYAN (wrong hue).
   - **(b) wrong FVF.** The hikari draws used FVF 0x142
     (XYZ|DIFFUSE|TEX1, no NORMAL) where retail uses 0x152
     (XYZ|NORMAL|DIFFUSE|TEX1). With D3DRS_LIGHTING on, no normals → FFP
     lighting fell back to flat vertex colour (cyan) instead of the
     normal-lit warm yellow-green. Root cause: `draw_loop_b_mesh` never
     set the FVF; it relied on the base pass's one-time
     SetVertexShader(0x152), which an earlier Pass-F/quad (FVF 0x142)
     clobbered before the hikari pass. The engine's per-mesh draw helper
     re-sets the FVF on every draw (retail ret_va 0xadf1b sets 0x152 56×
     per frame). Fix: set MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1 per mesh in
     `draw_loop_b_mesh` (like mesh_draw.c:51). → hue matched.

   Curtain-band mean abs diff vs retail: 19.2/21.6/16.4 (mood_para) →
   10.0/10.5/10.0 (null+FVF, balanced — residual is HUD overlap +
   640-resize alignment + the unported day/night clock tint).
   **Lesson:** when render state matches but the result diverges, diff
   the FVF — a missing NORMAL silently changes FFP-lit output. Lead any
   render-divergence with a zoom DIFF — see
   [[feedback_zoom_diff_render_debug]].
2. **Frustum over the LEFT window** — a visible opaque frustum/shape over
   the left window even though the god rays themselves look right. Likely
   a hikari god-ray billboard/quad rendering opaque (wrong blend or a mesh
   that should be additive-only).
3. **Missing god-ray/glow from the MIDDLE window** — retail emits a glow
   shaft from the centre window that the port does not draw at all. A
   missing draw (an additive billboard or hikari submesh the port's pass
   skips). Cross-check vs the retail trace's DrawPrimitiveUP set
   (FUN_0045a56f / FUN_0046f648 / FUN_00405354 / FUN_00406241) and the
   stubbed walker passes.
4. **Missing small shadow at the base of the centre table** — retail
   draws a contact shadow under the table that the port omits. Likely a
   blob/decal shadow draw (a small dark additive/modulate quad under
   furniture) in a pass the port hasn't ported.

> Methodology: each of these has retail ground truth in
> `runs/retail-d3d-house/d3d_trace.jsonl` (frame 14000) — diff per-draw
> state + the DrawPrimitiveUP/extra-indexed draws the port is missing
> against `runs/port-d3d-house2` to localize each.

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

## RESOLVED (2026-05-29) — the uniform ~2× brightness was a COLOROP mistype

**Root cause: the base room/furniture pass used `MODULATE` where the
engine uses `MODULATE2X` (a literal ×2 on texture×diffuse).**  Found via
a D3D state-trace A/B (D.4/D.5/D.6): captured the port at the HOUSE frame
(`runs/port-d3d-house`) and retail at frame 14000 (`runs/retail-d3d-house`),
then diffed per-draw COLOROP.  Retail drew 81/82 room submeshes under
COLOROP=5 (MODULATE2X); the port drew 42/55 under COLOROP=4 (MODULATE).

The setter is `FUN_00454f03` (`palette[0x1a40] % 7` → COLOROP), which the
port had ported (`scene1_apply_palette_combiner_mode`) but with **two
bugs**: (1) it wrote `D3DTSS_COLORARG2` instead of `D3DTSS_COLOROP`
(type 3 vs type 1 — the value table {2,4,5,7,8,10,11} are D3DTEXTUREOP
codes, not D3DTA args); (2) `scene1_palette_combiner_mode()` was stubbed
to `0` instead of reading `rec->drawcode`.  HOUSE is `drawcode:2` →
`2%7==2` → MODULATE2X.  The mistype was masked because the stubbed mode 0
mapped to `map[0]=2=D3DTA_TEXTURE`, a harmless COLORARG2 value, so
texturing still looked right while base brightness was halved.

Fix (commit after f1c7b2f): both corrected in `src/scene1_render.c`.
Post-fix port floor-centre RGB (227,176,79) ≈ retail (207,162,70), up
from the old (90,70,28) (~0.43× → ~1.09×).  Re-capture
`runs/port-d3d-house2/d3d_trace.jsonl`: all 55 room/furniture draws now
COLOROP=5.  Methodology note: this is the canonical D3D-trace-diff win —
the divergence was invisible to decompile reading (the port "looked
right") and only surfaced under a per-draw render-state A/B.

### Residual HOUSE gaps (separate subsystems, not brightness)

Retail draws more total ops than the port at the HOUSE frame (974 vs
663): +29 `DrawPrimitiveUP` (the 2D HUD via FUN_0040a765 / C7i + sprite
overlays) and ~27 extra indexed draws from the still-stubbed walker
passes (FUN_0045aa36 narrow-followup, FUN_00456f56 wide-b,
FUN_004176ff chr).  These add the HUD + Recette + extra effect layers,
not base-room brightness.  Tracked separately below + in the render
ladder.

## (historical) the uniform ~2× brightness investigation

- **Symptom:** retail HOUSE (`/mnt/c/Users/headpats/Documents/house-3d.png`)
  has bright, blinding white-cyan ray shafts blooming out of the windows
  and a generally brighter scene. Our render has the correct base
  lighting + the soft hikari-submesh glow, but is darker everywhere.
- **Retail ground truth (2026-05-29, `tools/frida_capture.py` HOUSE drive,
  frame 14000):** retail renders HOUSE at **640×480** (ours is 1024×768).
  Side-by-side: `runs/retail-house/diff_ours_vs_retail.png`; retail frame
  `runs/retail-house/frames/frame_14000.bmp`.  Retail is **~1.85–2× brighter
  everywhere**, not just at the windows:
  - floor centre (away from windows): ours (96,74,36) vs retail (178,139,70)
  - window/god-ray zone: ours (93,88,62) vs retail (182,171,123)

### `FUN_00459847(3)` RULED OUT as the cause (2026-05-29)

The earlier hypothesis ("the bright shafts are the missing
`FUN_00459847(3)` additive layer") is **wrong**.  `FUN_00459847` is the
additive **combat** projectile/effect billboard renderer: it walks
`&DAT_0695f004` (the combat projectile/effect table, stride 0xa8, 512
slots) and draws each record whose per-type render-class
(`*(int *)(&DAT_005c4cac + type*0x24)`) equals `param_1`.  The class-3
("additive") types are `{0x16,0x1b,0x1d,0x22,0x23,0x24,0x26,0x28,0x2e,0x61}`
— all combat projectile/effect types (const `.data` table at 0x5c4cac,
objdump file off 0x1c3490).  **In the HOUSE shop no projectiles spawn, so
the table is empty and `FUN_00459847(3)` draws nothing.**  It cannot be
the source of a *uniform* room-wide brightness deficit (an additive ray
layer would bloom at the windows, not lift the floor centre too).  The
table writers are combat code (spawn `FUN_0043a5d9`; sentinel -1 at
decomp L36121/L46532; readers `FUN_0043865e/441aab/442cef/45aa36`), which
corrects the 2026-05-26 "PHC #26 writerless" survey claim.  Porting
`FUN_00459847` is legitimate **combat-scene** fidelity work but is
unrelated to this gap — verify it with a dungeon/combat capture, not HOUSE.

### Real suspects for the uniform ~2× deficit (next visual session)

The maplight VALUE computation is faithful to `FUN_00458f67` (HOUSE
daytime mode-3 row 0: diffuse (0.8,0.8,0.9), ambient (0.6,0.6,0.6);
`SetLight`+`LightEnable(0,TRUE)`+`D3DRS_LIGHTING=TRUE` all match — see
`src/scene1_maplight.c`).  So the deficit is downstream of the light
values.  Candidates, cheapest first:
1. **Mesh material** — FFP lighting multiplies the light by the mesh
   MATERIAL (diffuse/ambient).  D3D8's default material is all-zero →
   lit geometry would be black unless a material is set.  Verify the
   room/furniture draws set `SetMaterial` with the values the engine
   uses (the room mesh's embedded material, or an engine-set default).
   A material at ~0.5 vs retail's ~1.0 would give exactly the ~2× gap.
2. **Global `D3DRS_AMBIENT`** — if the engine sets the global ambient
   render state (139/0x8B) to white-ish and the port leaves it default
   black, every lit vertex loses its global-ambient term uniformly.
3. **MODULATE vs MODULATE2X** — the base-pass COLOROP was reported as
   MODULATE in both; re-confirm per-stage (a MODULATE2X somewhere
   doubles brightness, which is exactly a ~2× factor).
   Capture both sides' D3D state (D.4/D.5 d3d_trace) at the HOUSE frame
   and diff SetMaterial + D3DRS_AMBIENT + the per-stage COLOROP.
Retail also draws the 2D HUD + Recette (separate subsystems).

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
