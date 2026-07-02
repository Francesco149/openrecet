/*
 * scene1_maplight — per-stage FFP map light builder (port of FUN_00458f67).
 *
 * FUN_00458f67 (2118 B) is called from scene1_render_meshes (FUN_00459dfd
 * L199, decomp all.c L54513) right before the mesh-walker passes fire.
 * It builds a single D3DLIGHT8 directional light at engine DAT_06a49a40
 * from the current stage record's `maplight` mode (+ the lightdir/
 * lightcolor/lightamb or maplight_d/maplight_a fields), then
 * SetLight(0) + LightEnable(0,TRUE) + SetRenderState(LIGHTING,TRUE).
 *
 * Earlier this was stubbed as scene1_walk_pre_dispatch_TODO with the
 * note "purpose unknown".  It is the scene-1 lighting source; without
 * it the HOUSE shop renders unlit (flat vertex-colour) instead of the
 * engine's per-vertex lit gradient.  See
 * docs/findings/scene1-house-render-gaps.md.
 *
 * The `maplight` mode values (documented in the game's own
 * idx/stageidx.txt):
 *   0 = light disabled
 *   1 = dungeon: animated diffuse/ambient pair (maplight_d/_a +
 *       maplightspeed) pulsed by sin/cos of the per-frame draw counter
 *   2 = dungeon: static lightdir / lightcolor / lightamb
 *   3 = town: time-of-day directional light from a 3-row preset table
 *
 * HOUSE (stage:0-1) is `maplight:3`.  The time-of-day interpolation is
 * driven by two inputs the caller passes through (engine reads them as
 * globals at L53746/L53762):
 *   - `shoptime`   = DAT_0450fb88[slot] (working bank dword CLOCK_TARGET,
 *                    0=morning .. 4=day-end); the integer preset selector.
 *   - `clock_phase`= DAT_0438b7d4, the animated phase eased +0.005/frame
 *                    toward shoptime (sim.c sim_step_a tail); the interp frac.
 * Curve (FUN_00458f67 L53746-93): shoptime<2 → row0 (day); shoptime==2 →
 * lerp(row0,row1) as phase 1→2 (day→dusk); shoptime==3 → lerp(row1,row2)
 * as phase 2→3 (dusk→night); shoptime>=4 → row2 (night).  Ported for the
 * day-end dusk-tint (RE §21.32; ground-truth: shoptime 1→2 at the customer-
 * leave completion, clock eases 1→2 over ~200 frames).
 */

#ifndef OPENRECET_SCENE1_MAPLIGHT_H
#define OPENRECET_SCENE1_MAPLIGHT_H

#include "tables_stage.h"   /* stage_record_t */

/* Current scene-1 stage record — the port analog of engine DAT_068dd2f0
 * (= &DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c, a pointer INTO the parsed
 * stage.idx table).  Returns the parsed record for the active stage, or
 * NULL if the stage table failed to load.
 *
 * The engine parses stage.idx straight into the DAT_068dd2f8 table that
 * DAT_068dd2f0 indexes, so the live stage palette IS the parsed record
 * (maplight / lightdir / hikaridrawcode / drawcode / fog all come from
 * stage.idx).  The port parses into g_stage.records[]; this accessor is
 * the bridge.  (g_stage_palette is a separate, mostly-zero scratch used
 * by the clear-colour + ambient-spawn paths only — see the erratum in
 * docs/findings/scene1-house-render-gaps.md; unifying the two is a
 * follow-up.)
 *
 * Today the active stage is fixed at HOUSE (stage:0-1 = records[0]);
 * when stage transitions land this follows the current stage index. */
const stage_record_t *scene1_current_stage_record(void);

/* Pure (host-testable) light value computation.  Fills `out` with the
 * directional light's diffuse / ambient / direction triples + the FFP
 * light type, and the post-clamp chr-ambient triple.  Returns:
 *   1 → caller should SetLight(0) + LightEnable(0,TRUE) + LIGHTING=TRUE
 *   0 → lighting disabled (mode 0 with the engine's disable flag set);
 *       caller should LightEnable(0,FALSE) + LIGHTING=FALSE
 * `rec` may be NULL (→ returns 0, lighting off). */
typedef struct scene1_maplight_values_s {
    int   type;          /* D3DLIGHTTYPE: 3 = D3DLIGHT_DIRECTIONAL */
    float diffuse[3];    /* D3DLIGHT8.Diffuse.rgb  */
    float ambient[3];    /* D3DLIGHT8.Ambient.rgb  */
    float direction[3];  /* D3DLIGHT8.Direction.xyz */
    float chr_ambient[3];/* engine DAT_06a49acc/ad0/ad4: ambient + chrlightoffset, clamped [0,1] */
} scene1_maplight_values_t;

int scene1_maplight_compute(const stage_record_t *rec,
                            int shoptime, float clock_phase,
                            scene1_maplight_values_t *out);

#ifdef _WIN32
struct IDirect3DDevice8;

/* FUN_00458f67 — build the D3DLIGHT8 from the current stage record and
 * push it to the device (SetLight/LightEnable/LIGHTING).  Caches the
 * built light so scene1_maplight_rebind() can re-apply it at the engine's
 * second light site (scene1_render_meshes L220-230). */
void scene1_build_maplight(struct IDirect3DDevice8 *dev);

/* scene1_render_meshes L220-230 re-apply: when `enable`, SetLight(0,
 * cached) + LightEnable(0,TRUE) + LIGHTING=TRUE; else LightEnable(0,
 * FALSE) + LIGHTING=FALSE.  Mirrors decomp all.c L54534-L54544. */
void scene1_maplight_rebind(struct IDirect3DDevice8 *dev, int enable);
#endif

#endif /* OPENRECET_SCENE1_MAPLIGHT_H */
