/*
 * scene1_render.c — see scene1_render.h for the chip writeup.
 *
 * C7f + C7g + C7h: line-by-line ports of FUN_0045bbf9, FUN_0045404b,
 * and FUN_00417504 — the three small render-frame brackets around the
 * scene-1 mesh walker (FUN_0040a765, C7j+).
 */

#include "scene1_render.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "math3d.h"
#include "mesh_draw.h"
#include "render_quad.h"
#include "mesh.h"            /* mesh_t (scene1_walk_initial_asset cast) */
#include "scene1_alpha_walker.h"
#include "call_trace.h"
#include "scene1_camera.h"
#include "scene1_emit_record.h"  /* scene1_emit_record — PII.1 */
#include "scene1_fx_overlays.h"  /* scene1_fx_overlays — FUN_00454191 scaffold */
#include "scene1_overlay.h"  /* scene1_overlay_render — 4-site dispatcher wiring */
#include "scene1_records.h"
#include "scene1_shop_walker.h"
#include "scene1_walker_pass_init.h"  /* PII.3a/PII.3b walker pass-init */
#include "scene1_wide_followup.h"
#include "sim.h"

/* ─── engine globals — module-local mirrors ─────────────────────────── */

/* DAT_073de29c — view matrix.  Identity at boot (engine BSS is zero,
 * but a zero matrix would push degenerate transforms to D3D; the
 * engine's first camera-pose tick overwrites the BSS-zero value before
 * any render call observes it).  Identity keeps the structure honest
 * until FUN_00441c3e + FUN_004424e7 port and start writing real poses. */
static float g_scene1_view[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

/* DAT_073de2dc — projection matrix. Rebuilt every frame by
 * scene1_render_camera_setup; no need to seed at boot. */
static float g_scene1_proj[16];

/* DAT_073de3a0 — fov in degrees. all.c:34225 immediate is 0x42340000
 * which is 45.0f exactly.  Reads/writes match the engine's float
 * load/store pattern. */
static float g_scene1_fov_deg = 45.0f;

/* ─── public accessors ──────────────────────────────────────────────── */

float *scene1_render_view_matrix(void)        { return g_scene1_view; }
const float *scene1_render_proj_matrix(void)  { return g_scene1_proj; }
float scene1_render_fov_deg(void)             { return g_scene1_fov_deg; }
void  scene1_render_set_fov_deg(float deg)    { g_scene1_fov_deg = deg; }

void scene1_render_reset_view(void)
{
    static const float ident[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    for (int i = 0; i < 16; i++) g_scene1_view[i] = ident[i];
}

/* ─── deferred sub-call stubs ───────────────────────────────────────── */

/* FUN_00441c3e (2217 B) + FUN_004424e7 (429 B) landed as Cc.1
 * (2026-05-23) in `src/scene1_camera.{c,h}`.  Default-path HOUSE
 * faithful: writes g_scene1_camera_eye/lookat + g_scene1_camera_orient
 * + the existing g_scene1_camera_anchor alias.  Camera shake (block L
 * of FUN_00441c3e), the cinematic counter ramp (block F), the class-1
 * post-load transition (block D), and the debug HUD overlays (block J)
 * are all deferred to Cc.2-4. */

/* FUN_00454191 (1391 B) — screen-effect overlays.  Landed 2026-05-27 PM
 * as a scaffold port in src/scene1_fx_overlays.{c,h}.  Entry probe +
 * outer-gate structure only; the three inner render branches stay
 * deferred until the counter starters port. */

/* FUN_00452f58 (491 B) — HUD camera + projection setup for the
 * overlay pass.  Landed 2026-05-24 as chip O.11 in scene1_overlay.c
 * (scene1_overlay_setup).  See scene1_overlay.h for the math + the
 * pre-matrix (DAT_0438cdf8) writer resolution (PHC #16). */

/* FUN_00414ee2 (4006 B) — the 2D overlay dispatcher — landed in chips
 * O.2..O.7 (src/scene1_overlay.{c,h}).  scene1_render_overlay below
 * calls scene1_overlay_render(dev, layer, 1) at the 4 layer sites. */

/* ─── C8a — scene1_render_meshes sub-call stubs ───────────────────── */

/* L51-L81 counter scan lives in scene1_records.c (C8g.1, 2026-05-23)
 * as scene1_records_counter_scan().  Wired below at the top of
 * scene1_render_meshes; the tables are sentinel-empty until the sim
 * populator FUN_0040fb3a lands, so every count lands 0. */

/* FUN_00457714 (5323 B) — per-pass cache-slot-anchored mesh walker
 * (PII.0 corrected the original "NPC walker" interpretation).
 *
 * Two main branches inside an outer DAT_073dfcec==0 gate (alpha-pass
 * guard, dead in retail):
 *
 *   HOUSE branch (stage palette mode < 1 at L52658):
 *     - setup phase 1 (L52671): per-mesh transforms for DAT_068dcca0
 *       (wall/floor/jutan) into local_738, gated DAT_0438bfb0 != 0.
 *       NOT ported (PII.3c scope).
 *     - setup phase 2 (L52704): per-mesh transforms for DAT_073b1ac8
 *       (shop_table; populated by C0A worker / src/scene_table.c).
 *       Ported as PII.3a (scene1_walker_phase2_compute).
 *     - outer slot loop (L52809, gated DAT_073cb108 != 0):
 *         - per-slot flag-byte dispatch picks a SetTexture target
 *         - DRAW LOOP A (L52902): DAT_068dcca0 wall/floor/jutan
 *           mesh draw.  NOT ported (PII.3c scope).
 *         - DRAW LOOP B (L52952, the shop_table furniture renderer):
 *           gated DAT_073dddb4 == 0 (status-screen NOT open) AND
 *           DAT_0438bfb4 != 0.  Ported as PII.3b
 *           (scene1_walker_pass_render_house).
 *
 *   DUNGEON branch (L53046+): different mesh sources, parallel
 *   structure.  Not ported.
 *
 * Called with arg=0 from scene1_render_meshes (this file via
 * scene1_walk_pre_pass) and arg=1 (via scene1_walk_alpha_pre); also
 * called with arg=2/3 from FUN_00458bdf (alpha walker, still stubbed). */
static void scene1_walk_pass_init(IDirect3DDevice8 *dev, int which_pass)
{
    /* PII.3b: HOUSE branch outer loop + draw loop B.  The HOUSE-branch
     * gate (decomp L52658: stage palette mode < 1) is NOT checked
     * here — scene1_render_meshes is the only caller and the engine's
     * only HOUSE-mode dispatcher.  Once a DUNGEON port lands, add the
     * gate accessor + branch. */
    scene1_walker_pass_render_house((struct IDirect3DDevice8 *)dev,
                                    which_pass);
}

/* Forward decls for accessors used by scene1_walk_initial_asset (PII.1).
 * Definitions live further down in the engine-state accessor section
 * so the dormant-stub defaults are co-located with their siblings. */
static int   scene1_palette_initial_asset_flag(void);
static void *scene1_initial_asset_ptr(void);

/* FUN_00455191 (217 B) — per-texture-cache-slot single-mesh draw
 * helper.  Already ported as `scene1_emit_record` in
 * src/scene1_emit_record.c (C8e.bridge, 2026-05-24).  The PII.survey
 * landing's "NPC" naming was misleading: the outer loop iterates
 * g_mesh_tex_cache slots, not NPCs (see PII.0 findings in
 * docs/findings/scene1-walker-pass-init.md).
 *
 * Body shape (decomp L51528 / asm 0x455191):
 *
 *   for (slot = 0; slot < g_mesh_tex_cache.count; slot++) {
 *       if (mesh->vtable == 0) continue;
 *       FUN_00454fe4(slot);  // == scene1_emit_apply_material_state
 *       for (mat_i = 0; mat_i < mesh->material_count; mat_i++) {
 *           if (mesh->texture_slots[mat_i] == slot) {
 *               if (first) SetTexture(0, g_mesh_tex_cache.entries[slot].sprite);
 *               SetMaterial(&mesh->materials[mat_i]);
 *               mesh->vtable->DrawSubset(mat_i);
 *           }
 *       }
 *   }
 *
 * Callers (4 sites in retail; all already wired via scene1_emit_record):
 *   - shop_walker Pass A/B/C/D — scene1_shop_walker.c L224/276/283/293/341
 *   - alpha_pre walker (FUN_0045672a L52185/L52209) — still a TODO stub
 *
 * (The L52952 inner loop of FUN_00457714 is an inlined version of
 *  this same idiom over an array of meshes — chip PII.3b.)
 *
 * Caller at L165 of scene1_render_meshes gates on palette+0x108 != 0
 * AND DAT_068dcf98 != 0 — palette+0x108 is zero for HOUSE
 * (scene1_preload.c:140), so the L165 site itself is dormant in
 * HOUSE.  Wired below as `scene1_walk_initial_asset` (PII.1, 2026-05-26):
 * composes the engine's S(-2.8, 2.8, 2.8) * T(0,0,0) world matrix
 * + SetTransform + scene1_emit_record.  Bit-exact under BSS-zero
 * gates; surfaces under any non-HOUSE scene that sets both. */
static void scene1_walk_initial_asset(IDirect3DDevice8 *dev)
{
    /* Engine FUN_00459dfd L160-L166 (decomp all.c L54474-L54479):
     *   thunk_FUN_004a3462(&trans, 0, 0, 0);                    // MatrixTranslation
     *   thunk_FUN_004a33d2(&scale, -2.8, 2.8, 2.8);             // MatrixScaling
     *   thunk_FUN_004a2a03(&world, &scale, &trans);             // Multiply
     *   SetTransform(D3DTS_WORLDMATRIX(0), &world);             // vtable[0x94]
     *   FUN_00455191(&DAT_068dcf98);                            // == scene1_emit_record
     *
     * Constants: 0xc0333333 = -2.8f, 0x40333333 = 2.8f.  Translation
     * is the zero vector — composed matrix collapses to pure scaling. */
    float trans[16];
    float scale[16];
    float world[16];
    mat4_translation(trans, 0.0f, 0.0f, 0.0f);
    mat4_scaling(scale, -2.8f, 2.8f, 2.8f);
    mat4_mul(world, scale, trans);
    IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                  (const D3DMATRIX *)world);
    scene1_emit_record((struct IDirect3DDevice8 *)dev,
                       (const mesh_t *)scene1_initial_asset_ptr());
}

/* FUN_00405d70 (911 B) — depth-generation pre-pass.  Likely emits
 * the floor's depth-only draw so later passes can Z-reject.  Sized
 * for a 1-chip port on its own. */
static void scene1_walk_depth_prepass_TODO(void)
{
    /* TODO C8-followup: port FUN_00405d70. */
}

/* FUN_00458f67 (2118 B) — sub-pass at L199, before any walker fires.
 * Purpose unknown; possibly a per-frame palette or scratch-texture
 * regeneration. */
static void scene1_walk_pre_dispatch_TODO(void)
{
    /* TODO C8-followup: port FUN_00458f67. */
}

/* FUN_00459847 (1444 B) — projectile-table renderer (NOT walls/floor;
 * earlier comment was wrong).  Survey 2026-05-26 (decomp all.c L53983):
 * walks &DAT_0695f004 stride 0xa8 (the PHC #26 projectile table),
 * filters by TYPE != -1 + slot[+0x77] != 3 + per-type class match
 * (`*(int *)(&DAT_005c4cac + iVar3 * 0x24) == param_1`), and emits via
 * the scene-tree dispatcher (FUN_00403d79/FUN_00404870/FUN_00404a20).
 * Called 4× per frame with class param ∈ {0, 1, 2, 3} — 0/1 from
 * scene1_render_meshes (this file), 2/3 from FUN_00458bdf (alpha walker).
 *
 * Doubly dormant in retail: the projectile table at 0x695f004 has no
 * writers anywhere in the binary (PHC #26), and the per-type class
 * attr table at 0x5c4cac also has no writers (PHC #19).  Porting this
 * yields zero visible pixels until something seeds those tables.
 *
 * The TRUE walls/floor draw is `FUN_00455191(&DAT_068dcf98)` at L165
 * of scene1_render_meshes — see scene1_walk_initial_asset_TODO below.
 * For HOUSE specifically, palette+0x108 is zero (no 3D room mesh) — the
 * shop interior renders as 2D sprites + furniture via shop_walker. */
static void scene1_walk_narrow_frustum_TODO(int pass)
{
    (void)pass;
}

/* FUN_0045aa36 (4493 B) — sub-pass between FUN_00459847(0) and the
 * fog-enable branch.  Substantial — multi-chip on its own. */
static void scene1_walk_narrow_followup_TODO(void)
{
    /* TODO C8-followup: port FUN_0045aa36.  May overlap with the
     * narrow walker; survey first. */
}

/* FUN_004552d0 (5210 B) — the WIDE-frustum mesh walker.  Landed
 * 2026-05-23 as C8c (src/scene1_shop_walker.{c,h}) with all 7
 * walker passes structured + state writes verbatim; per-record
 * draw helpers (FUN_00455191 / FUN_00456d48 / FUN_0045a56f /
 * FUN_00404a20 chain) remain TODO stubs because every walker
 * pass is dormant in HOUSE today (BSS-zero count globals and
 * record active flags). */

/* FUN_004161c7 (4925 B) — sub at L219, right after FUN_004552d0.
 * Landed 2026-05-23 as C8f.1 (src/scene1_wide_followup.{c,h}) with full
 * state-writes + per-pass body stubs + Pass F integration with the
 * existing scene1_pass_f module.  Per-pass inner draws (A/B/C/D/E)
 * remain TODO stubs because the engine's wide-followup data populator
 * for tables B/C is unported — every pass is dormant in HOUSE. */

/* FUN_0045672a (1317 B) — sub at L246, before FUN_00458bdf.  Likely
 * the alpha-pass setup helper. */
static void scene1_walk_alpha_pre_TODO(void)
{
    /* TODO C8-followup: port FUN_0045672a. */
}

/* FUN_00458bdf (904 B) — alpha-pass mesh walker.  Landed 2026-05-23
 * as C8d (src/scene1_alpha_walker.{c,h}) with full state-writes +
 * branch structure.  Two inner FUN_00459847 walker calls stay TODO
 * (same stub as scene1_walk_narrow_frustum_TODO below). */

/* FUN_00456f56 (1982 B) — second wide-frustum pass mesh walker.
 * Likely handles a different draw-order category (transparent
 * props? particle quads in 3D space?). */
static void scene1_walk_wide_b_TODO(void)
{
    /* TODO C8-followup: port FUN_00456f56. */
}

/* FUN_004176ff (30395 B) — the chr (character avatar) mesh walker.
 * The biggest function in scene-1 render.  Will fan into many sub-
 * chips. */
static void scene1_walk_chr_TODO(void)
{
    /* TODO C8-followup: port FUN_004176ff.  Massive — survey
     * before any port attempt.  Almost certainly handles Recette,
     * Tear, and the visiting NPCs' animated meshes. */
}

/* FUN_00405b1a (598 B) — tail at L257.  Small. */
static void scene1_walk_tail_TODO(void)
{
    /* TODO C8-followup: port FUN_00405b1a. */
}

/* FUN_00454f03 (120 B) — TSS COLORARG2 from stage-palette mode
 * integer.  Ported inline — trivial enough that pulling it into a
 * separate file would cost more than it saves. */
static void scene1_apply_palette_combiner_mode(IDirect3DDevice8 *dev,
                                               int mode)
{
    /* Engine maps (mode % 7) to a TSS COLORARG2 value.  D3DTA names
     * (DIFFUSE=0, CURRENT=1, TEXTURE=2, TFACTOR=3, SPECULAR=4,
     * TEMP=5) only cover 0..5; values 7/8/10/11 fall outside the
     * documented enum but the engine writes them anyway (D3D8
     * retail runtime tolerates out-of-range here).  Reproduced
     * verbatim. */
    int m = mode % 7;
    if (m < 0) m += 7;
    static const DWORD map[7] = { 2, 4, 5, 7, 8, 10, 11 };
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2,
                                          map[m]);
}

/* ─── C7f — FUN_0045bbf9 port ───────────────────────────────────────── */

void scene1_render_camera_setup(struct IDirect3DDevice8 *dev_in)
{
    /* E.2 probe — FUN_0045bbf9 @ 0x45bbf9. */
    CALL_TRACE_ENTER(0x45bbf9u);

    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L8-10 (45bbf9.c): "no shake / no menu" gate for the camera-
     * pose helper.  counter_998 is sim.c's DAT_06a49998 (cyclic 0..0x14
     * or 1..0xc depending on mode); counter_6fa4 (DAT_06a46fa4) is a
     * separate menu/dialog gate that has no porter yet — stays at 0,
     * which keeps the gate open.
     *
     * Cc.1: pose_compute writes eye/lookat; build_view_matrix writes
     * the D3DTS_VIEW matrix.  The engine inlines the view-matrix build
     * at the tail of FUN_00441c3e (via FUN_0040120c); we keep it
     * separate so the gate-closed branch can leave the prior matrix
     * untouched without rebuilding from a partially-updated state. */
    if (sim_get_counter_998() == 0 /* && counter_6fa4 == 0 */) {
        scene1_camera_pose_compute();
        scene1_camera_build_view_matrix(g_scene1_view);
    }
    /* L11: unconditional angle update. */
    scene1_camera_angle_compute();

    /* L12: SetTransform(D3DTS_VIEW, &g_scene1_view). */
    IDirect3DDevice8_SetTransform(dev, D3DTS_VIEW,
                                  (const D3DMATRIX *)g_scene1_view);

    /* L13: FUN_004a3ee8 = mat4_perspective_fov_rh.  Args verbatim:
     *   - fov_y in radians = fov_deg * π/180  (engine constant
     *     0.017453292 = π/180 to single-precision)
     *   - aspect = 0x3faaaaab = 4/3 exact (the engine fixes this even
     *     when the back buffer is widescreen — recet_ini's resolution
     *     setting affects the BB extent + 2D scaling but the scene
     *     projection stays 4/3)
     *   - z_near = 0x3f800000 = 1.0
     *   - z_far  = 0x43af0000 = 350.0
     */
    {
        float fov_rad = g_scene1_fov_deg * 0.017453292f;
        mat4_perspective_fov_rh(g_scene1_proj,
                                fov_rad,
                                4.0f / 3.0f,
                                1.0f,
                                350.0f);
    }

    /* L14: SetTransform(D3DTS_PROJECTION, &g_scene1_proj). */
    IDirect3DDevice8_SetTransform(dev, D3DTS_PROJECTION,
                                  (const D3DMATRIX *)g_scene1_proj);

    /* L15: FUN_00459dfd — the actual scene-1 mesh dispatcher.  The
     * engine doesn't separate "render state setup" from "draw the
     * meshes" — both happen inside FUN_00459dfd.  Our C7b port of
     * its L86..L198 prefix lives in mesh_draw.c as
     * `mesh_set_default_render_state` (still used by --show-mesh);
     * the full L51..L257 port lives in scene1_render_meshes (C8a)
     * with all four pass-walker call sites stubbed for follow-up
     * chips. */
    scene1_render_meshes(dev);
}

/* ─── C8a — FUN_00459dfd port ───────────────────────────────────────── */

/* Per-frame counters DAT_06a49b28 / DAT_06a49b24.  Engine bumps these
 * inside FUN_00459dfd; mirroring them so future ports of consumers
 * (the per-walker animation index, the per-frame randomness seed)
 * inherit the same numbers we wrote here. */
static uint32_t g_scene1_phase_counter = 0;  /* DAT_06a49b28 */
static uint32_t g_scene1_draw_counter  = 0;  /* DAT_06a49b24 */

uint32_t scene1_render_phase_counter(void) { return g_scene1_phase_counter; }
uint32_t scene1_render_draw_counter(void)  { return g_scene1_draw_counter; }

/* ─── engine-state TODO accessors ──────────────────────────────────────
 *
 * The C8a body reads a half-dozen engine globals + several stage_palette
 * fields that aren't typed yet.  Putting them behind named accessors
 * means each typed-field follow-up replaces ONE accessor without
 * touching the body of scene1_render_meshes — and the names document
 * what the BSS-zero default does today.
 *
 * Every accessor returns the BSS-zero value (zero / 0.0f / NULL) that
 * makes its caller branch dormant in HOUSE.  A follow-up chip turning
 * one on touches just the accessor and any global it needs.
 */

/* palette + 0x1a38 (float fog_start).  Non-zero → enable fog. */
static float scene1_palette_fog_start(void) { return 0.0f; }

/* palette + 0x1a3c (float fog_end). */
static float scene1_palette_fog_end(void) { return 0.0f; }

/* palette + 0x1a40 (int combiner mode).  Indexes scene1_apply_palette
 * _combiner_mode's TSS COLORARG2 table mod 7. */
static int scene1_palette_combiner_mode(void) { return 0; }

/* palette + 0x1a90/94/98 (int per-channel fog color bytes). */
static int scene1_palette_fog_color_r(void) { return 0; }
static int scene1_palette_fog_color_g(void) { return 0; }
static int scene1_palette_fog_color_b(void) { return 0; }

/* palette + 0x1ae0 (int lighting enable).  Non-zero → enable D3DRS
 * _LIGHTING + light 0 from DAT_06a49a40. */
static int scene1_palette_lighting_enabled(void) { return 0; }

/* palette + 0x108 (int initial-asset flag).  Non-zero → run the L160-
 * L166 initial-asset draw block (needs DAT_068dcf98 too). */
static int scene1_palette_initial_asset_flag(void) { return 0; }

/* DAT_068dcf98 — initial asset pointer.  Gates L160-L166 alongside
 * the palette flag above. */
static void *scene1_initial_asset_ptr(void) { return NULL; }

/* DAT_073dfcec — alpha-pass guard.  Zero = "render in normal mode" =
 * combiner-mode write proceeds.  No setter ported. */
static int scene1_alpha_pass_guard(void) { return 0; }

/* DAT_0438b4e4 — texture combiner override.  Non-zero = force
 * COLORARG2 mode 2 (D3DTA_TEXTURE).  No setter ported. */
static int scene1_combiner_override(void) { return 0; }

/* DAT_0438cd60 — fog override.  Non-zero (specifically == 1) forces
 * fog OFF regardless of palette settings.  No setter ported. */
static int scene1_fog_override(void) { return 0; }

/* DAT_0438b178 — recet.ini trilinear setting.  Zero (default) =
 * trilinear ON (MIPFILTER=LINEAR + LODBIAS set).  recet.ini layer
 * doesn't expose this yet. */
static int scene1_trilinear_off(void) { return 0; }

/* DAT_073dfcf0 — device's preferred LOD bias.  Set once at device
 * init by the engine; not exposed via our device wrapper. */
static DWORD scene1_device_lodbias(void) { return 0; }

/* Compute & push the standard scene-1 projection matrix with a
 * given z_far.  The engine swaps z_far between 350.0 (narrow, for
 * room geometry) and 2000.0 (wide, for distant scenery / chr) five
 * times inside FUN_00459dfd; mirroring as a helper keeps the body
 * readable.  Exposed publicly as scene1_render_push_projection so
 * the per-walker chip ports (C8c+) can re-use it. */
void scene1_render_push_projection(struct IDirect3DDevice8 *dev_in, float z_far)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    float fov_rad = g_scene1_fov_deg * 0.017453292f;
    mat4_perspective_fov_rh(g_scene1_proj, fov_rad,
                            4.0f / 3.0f, 1.0f, z_far);
    IDirect3DDevice8_SetTransform(dev, D3DTS_PROJECTION,
                                  (const D3DMATRIX *)g_scene1_proj);
}

/* Internal short alias so the existing scene1_render_meshes body
 * doesn't need to re-cast the device pointer through the public
 * entry's NULL guard on every call. */
static inline void scene1_push_projection(IDirect3DDevice8 *dev, float z_far)
{
    scene1_render_push_projection((struct IDirect3DDevice8 *)dev, z_far);
}

/* Public form of scene1_apply_palette_combiner_mode — same body, just
 * exposed for the per-walker chips.  Internal callers still use the
 * static form. */
void scene1_render_apply_palette_combiner_mode(struct IDirect3DDevice8 *dev_in,
                                               int mode)
{
    if (!dev_in) return;
    scene1_apply_palette_combiner_mode((IDirect3DDevice8 *)dev_in, mode);
}

/* Fog enable/setup helper — called three times from FUN_00459dfd
 * (L170-L184, L206-L214, L233-L241).  The first call writes the full
 * fog parameters; the later two re-write only FOGENABLE (the
 * parameters are sticky between calls).  Reproduced as-is. */
static void scene1_apply_fog_state(IDirect3DDevice8 *dev, BOOL full)
{
    BOOL enable = (scene1_palette_fog_start() != 0.0f)
                  && (scene1_fog_override() != 1);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, enable);
    if (!enable || !full) return;

    /* SetRenderState takes a DWORD — fog start/end are floats whose
     * bit pattern goes through verbatim.  memcpy keeps strict
     * aliasing happy. */
    float fog_start = scene1_palette_fog_start();
    float fog_end   = scene1_palette_fog_end();
    DWORD fog_start_bits, fog_end_bits;
    memcpy(&fog_start_bits, &fog_start, sizeof fog_start_bits);
    memcpy(&fog_end_bits,   &fog_end,   sizeof fog_end_bits);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGSTART, fog_start_bits);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGEND,   fog_end_bits);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGTABLEMODE,  0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGVERTEXMODE, 3);
    /* Engine packs fog color from three int globals by OR-shift —
     * each int's low byte ends up at the correct ARGB position with
     * alpha = 0xff. */
    DWORD fog_color =
        (((scene1_palette_fog_color_r() | 0xffffff00) << 8)
         | (scene1_palette_fog_color_g() & 0xff)) << 8
        | (scene1_palette_fog_color_b() & 0xff);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGCOLOR, fog_color);
}

/* FUN_004597ad (48 B) — pre-walker[0] wrapper.  Two gates guard the
 * combiner-mode write; the inner sub-init runs unconditionally. */
static void scene1_walk_pre_pass(IDirect3DDevice8 *dev)
{
    if (scene1_alpha_pass_guard() == 0) {
        int mode = (scene1_combiner_override() != 0)
                       ? 2
                       : scene1_palette_combiner_mode();
        scene1_apply_palette_combiner_mode(dev, mode);
    }
    scene1_walk_pass_init(dev, 0);
}

/* FUN_004597dd (106 B) — alpha-pass-pre wrapper.  Same gates as
 * FUN_004597ad + three render-state writes after the inner sub. */
static void scene1_walk_alpha_pre(IDirect3DDevice8 *dev)
{
    if (scene1_alpha_pass_guard() == 0) {
        int mode = (scene1_combiner_override() != 0)
                       ? 2
                       : scene1_palette_combiner_mode();
        scene1_apply_palette_combiner_mode(dev, mode);
        scene1_walk_pass_init(dev, 1);
    }
    /* L18-19 of FUN_004597dd: SetTSS(0, ADDRESSU=0xd, WRAP=1) +
     * SetTSS(0, ADDRESSV=0xe, WRAP=1).  Engine resets the sampler's
     * address mode every alpha-pass (some menu code may have flipped
     * it to CLAMP). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU,
                                          D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV,
                                          D3DTADDRESS_WRAP);
    /* L20: D3DRS_CULLMODE=1=D3DCULL_NONE.  Alpha pass disables
     * culling because alpha-tested foliage etc. is double-sided. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
}

void scene1_render_meshes(struct IDirect3DDevice8 *dev_in)
{
    /* See CALL_TRACE_ENTER_STUB at L584 — body is wired but every walker
     * branch (depth_prepass / pre_dispatch / narrow_frustum / alpha_pre
     * / wide_b / chr / narrow_followup / tail) is a TODO stub that
     * returns immediately.  The probe firing on the port side means
     * scene1_render_meshes was entered, NOT that the engine's per-mesh
     * draws happened.  Marked stub so call_trace_diff doesn't show this
     * row as full-parity ` ` when it's actually ≈ (count-match-but-
     * body-not-doing-engine-work). */
    /* E.2 probe — FUN_00459dfd @ 0x459dfd.  STUB per the comment above. */
    CALL_TRACE_ENTER_STUB(0x459dfdu);

    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L51: bump per-frame phase counter by 0x11.  Read by the chr
     * walker as an animation phase (probably). */
    g_scene1_phase_counter += 0x11u;

    /* L52-L81: three "highest non-sentinel index" scans across the
     * per-record tables.  scene1_records_reset (called from
     * scene1_preload_house) has primed the sentinel state; the scan
     * lands 0/0/0 until the sim populator FUN_0040fb3a ports. */
    scene1_records_counter_scan();

    /* L86: D3DRS_CULLMODE = D3DCULL_CCW.  Engine starts every frame
     * with back-face culling on CCW-wound faces. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_CCW);

    /* L92, L98: TSS MINFILTER = MAGFILTER = LINEAR. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER,
                                          D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER,
                                          D3DTEXF_LINEAR);

    /* L100-L118: trilinear gate.  Engine writes:
     *   trilinear ON  (DAT_0438b178 == 0):
     *     SetTSS(0, MIPFILTER, LINEAR);
     *     SetTSS(0, MIPMAPLODBIAS, DAT_073dfcf0);
     *   trilinear OFF (DAT_0438b178 != 0):
     *     SetTSS(0, MIPFILTER, NONE);
     *
     * Note: engine default is trilinear ON (recet.ini sets the OFF
     * value); mesh_set_default_render_state in mesh_draw.c writes
     * MIPFILTER=NONE for the preview path, which is the OPPOSITE of
     * the engine default.  Once recet.ini exposes the trilinear
     * setting we'll reconcile. */
    if (scene1_trilinear_off() == 0) {
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER,
                                              D3DTEXF_LINEAR);
        IDirect3DDevice8_SetTextureStageState(dev, 0,
                                              D3DTSS_MIPMAPLODBIAS,
                                              scene1_device_lodbias());
    } else {
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER,
                                              D3DTEXF_NONE);
    }

    /* L122: SetVertexShader(FVF 0x152) — XYZ|NORMAL|DIFFUSE|TEX1. */
    IDirect3DDevice8_SetVertexShader(dev, 0x152);

    /* L127: LightEnable(0, FALSE) — disable light 0 at frame start. */
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);

    /* L132-L137: LIGHTING=FALSE, FOGENABLE=FALSE.  Engine disables
     * FFP lighting + fog for the initial sky pass; both get re-
     * enabled below based on stage palette. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING,  FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* L142-L147: ZENABLE=FALSE, ZWRITEENABLE=FALSE.  Initial sky-
     * pass state — no depth.  Re-enabled at L168-L169 and L242-L243
     * for the room and alpha passes. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);

    /* L148-L153: TSS COLOROP=MODULATE. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                          D3DTOP_MODULATE);

    /* L154-L159: First projection — z_far = 2000.0 for the sky pass
     * + initial-asset draw. */
    scene1_push_projection(dev, 2000.0f);

    /* L160-L166: initial transform asset.  Gated on stage palette
     * + 0x108 != 0 AND DAT_068dcf98 != 0.  Both BSS-zero in HOUSE
     * (palette+0x108 documented zero in scene1_preload.c:140), so
     * the branch is dormant; engine fidelity preserved bit-exactly.
     * scene1_walk_initial_asset (PII.1) composes the world matrix
     * and delegates to scene1_emit_record. */
    if (scene1_palette_initial_asset_flag() != 0
        && scene1_initial_asset_ptr() != NULL) {
        scene1_walk_initial_asset(dev);
    }

    /* L167: FUN_00405d70 (911 B) — depth-generation pre-pass. */
    scene1_walk_depth_prepass_TODO();

    /* L168-L169: re-enable depth for the room pass. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);

    /* L170-L184: fog gate.  HOUSE palette has fog_start=0.0; branch
     * dormant.  scene1_apply_fog_state(dev, TRUE) writes the full
     * fog setup if enabled, else just FOGENABLE=FALSE. */
    scene1_apply_fog_state(dev, TRUE);

    /* L185: FUN_00454f03(palette + 0x1a40) — TSS COLORARG2 from
     * palette mode int. */
    scene1_apply_palette_combiner_mode(dev, scene1_palette_combiner_mode());

    /* L186-L187: project back to z_far = 350.0 for the room pass. */
    scene1_push_projection(dev, 350.0f);

    /* L188-L198: per-frame TSS + RS extras.
     *   TSS(0, ADDRESSU=0xd,    WRAP=1)
     *   TSS(0, ADDRESSV=0xe,    WRAP=1)
     *   RS(0x80=PATCHEDGESTYLE, 0)
     *   RS(0x8b=AMBIENT,        0xff000000)
     *   RS(0x8d=COLORVERTEX,    1)
     *   RS(0x19=ALPHAFUNC,      5=D3DCMP_GREATER)
     *   RS(0x91=DIFFUSEMATERIALSOURCE, 1=COLOR1)
     *   RS(0x93=AMBIENTMATERIALSOURCE, 1=COLOR1)
     *   TSS(0, 3=COLORARG2,     2=D3DTA_TEXTURE)
     *   TSS(0, 2=COLORARG1,     0=D3DTA_DIFFUSE)
     *   RS(9=SHADEMODE,         2=GOURAUD)
     */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_PATCHEDGESTYLE,         0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT,                0xff000000u);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_COLORVERTEX,            TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAFUNC,              D3DCMP_GREATER);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DIFFUSEMATERIALSOURCE,  D3DMCS_COLOR1);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENTMATERIALSOURCE,  D3DMCS_COLOR1);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2,    D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1,    D3DTA_DIFFUSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SHADEMODE,              D3DSHADE_GOURAUD);

    /* L199: FUN_00458f67 (2118 B) — pre-dispatch sub-pass. */
    scene1_walk_pre_dispatch_TODO();

    /* L200-L201: re-establish z_far=350 (idempotent — already there). */
    scene1_push_projection(dev, 350.0f);

    /* L202: bump per-frame draw counter. */
    g_scene1_draw_counter++;

    /* L203-L205: pre-walker → first narrow-frustum walker → followup. */
    scene1_walk_pre_pass(dev);
    scene1_walk_narrow_frustum_TODO(0);
    scene1_walk_narrow_followup_TODO();

    /* L206-L214: fog gate, this time just FOGENABLE.  Same condition
     * as L170-L184; engine doesn't re-write the parameters. */
    scene1_apply_fog_state(dev, FALSE);

    /* L215: second narrow-frustum walker pass. */
    scene1_walk_narrow_frustum_TODO(1);

    /* L216-L217: WIDE projection (z_far = 2000.0). */
    scene1_push_projection(dev, 2000.0f);

    /* L218: ★ FUN_004552d0 ★ — the shop-interior walker (5210 B).
     * C8c (2026-05-23) ports the structure + state writes; per-
     * record draws are TODO stubs (dormant in HOUSE — see
     * scene1_shop_walker.h). */
    scene1_shop_walker((struct IDirect3DDevice8 *)dev);

    /* L219: ★ FUN_004161c7 ★ — the wide-followup walker (4925 B).
     * C8f.1 (2026-05-23) ports the structure + state writes; per-record
     * draws for passes A/B/C/D/E are TODO stubs (dormant in HOUSE —
     * see scene1_wide_followup.h).  Pass F delegates to the existing
     * scene1_pass_f module. */
    scene1_wide_followup((struct IDirect3DDevice8 *)dev);

    /* L220-L230: stage-palette lighting gate.  When palette + 0x1ae0
     * != 0, light 0 is populated from DAT_06a49a40 + enabled;
     * otherwise disabled.  HOUSE palette has 0 → lights off. */
    {
        BOOL lighting_on = (scene1_palette_lighting_enabled() != 0);
        if (lighting_on) {
            /* TODO C8-followup: populate light 0 from DAT_06a49a40 —
             * a per-stage D3DLIGHT8 scratch that a separate helper
             * (not yet identified) fills from the stage palette.
             * Until it's ported, LightEnable(0, TRUE) without a
             * SetLight call leaves whatever light 0 had previously
             * (or a zero light if never set). */
            IDirect3DDevice8_LightEnable(dev, 0, TRUE);
        } else {
            IDirect3DDevice8_LightEnable(dev, 0, FALSE);
        }
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, lighting_on);
    }

    /* L231-L232: project back to z_far = 350. */
    scene1_push_projection(dev, 350.0f);

    /* L233-L241: fog gate, third instance — same condition. */
    scene1_apply_fog_state(dev, FALSE);

    /* L242-L243: ZENABLE=TRUE, ZWRITEENABLE=FALSE — alpha pass. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);

    /* L244-L246: alpha-pre wrapper + post-write + sub-pass. */
    scene1_walk_alpha_pre(dev);
    /* L245: SetTSS(0, MIPFILTER=0x12, 0=NONE).  Engine drops mip-
     * mapping for the alpha pass — alpha-tested edges look better
     * without trilinear. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER,
                                          D3DTEXF_NONE);
    scene1_walk_alpha_pre_TODO();

    /* L247: FUN_00458bdf (904 B) — alpha-pass walker.  C8d
     * (2026-05-23) ports the structure + state writes; inner
     * FUN_00459847(2/3) calls remain stubbed (see
     * scene1_alpha_walker.h). */
    scene1_alpha_walker((struct IDirect3DDevice8 *)dev);

    /* L248-L251: WIDE projection → second wide walker. */
    scene1_push_projection(dev, 2000.0f);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER,
                                          D3DTEXF_NONE);
    scene1_walk_wide_b_TODO();

    /* L252-L254: WIDE projection (re-set, idempotent) → chr walker. */
    scene1_push_projection(dev, 2000.0f);
    scene1_walk_chr_TODO();

    /* L255-L257: project back to z_far=350 → tail. */
    scene1_push_projection(dev, 350.0f);
    scene1_walk_tail_TODO();
}

/* ─── C8b — per-frame emit adapter ──────────────────────────────────── */

void scene1_render_emit_frame(struct IDirect3DDevice8 *dev_in,
                              const float world_matrix[16],
                              const mesh_t *m)
{
    if (!dev_in || !m) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* Engine FUN_004047df L15: SetTransform(D3DTS_WORLDMATRIX(0),
     * &frame->world_matrix).  D3DTS_WORLDMATRIX(0) == 256 == 0x100,
     * which D3D8 aliases to D3DTS_WORLD.  Passing NULL skips the
     * write entirely (the engine never does this; for tests + debug
     * paths that pre-set the matrix themselves). */
    if (world_matrix) {
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLDMATRIX(0),
                                      (const D3DMATRIX *)world_matrix);
    }

    /* Engine FUN_00403eb7 — per-subset SetMaterial + SetTexture +
     * ID3DXMesh::DrawSubset.  Our mesh_draw_d3d8 inlines all three
     * (SetStreamSource once, then per-submesh SetIndices +
     * SetTexture + SetMaterial + DrawIndexedPrimitive), driven by
     * our flat mesh_t instead of ID3DXMesh's attribute table.  Same
     * D3D8 call output, different data source.
     *
     * See docs/findings/scene1-leaf-chain.md for the full mapping
     * + the skinned-mesh branch (FUN_00404209 / 03f23 / 04500 /
     * 04668) that's deferred to the chr walker chip. */
    mesh_draw_d3d8(dev, m);
}

/* ─── C7g — FUN_0045404b port ───────────────────────────────────────── */

void scene1_render_fx_tail(struct IDirect3DDevice8 *dev_in)
{
    /* E.2 probe — FUN_0045404b @ 0x45404b.  STUB: body's only payload
     * is the head call to scene1_fx_overlays (= FUN_00454191), scaffold-
     * ported 2026-05-27 PM (gates only, inner draws deferred); the
     * L20-gated inner draw is also dormant in HOUSE (sim_get_counter_994
     * BSS-zero) AND its post-tex source binding (DAT_073de648) is
     * unported too.  Mark stub so call_trace_diff surfaces it as ≈
     * instead of clean parity. */
    CALL_TRACE_ENTER_STUB(0x45404bu);

    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L19: full screen-effect overlay pass.  Body is a STUB scaffold —
     * gates match the engine; inner branches deferred. */
    scene1_fx_overlays((struct IDirect3DDevice8 *)dev);

    /* L20: gate.  DAT_0438b1b0 is a separate render-mode flag (zero in
     * normal scene-1; nonzero during certain menu/transition states).
     * counter_994 is sim.c's DAT_06a49994 — scene-shake counter, 0 by
     * default.  Both gates are zero/dormant today, so this branch is
     * unreachable.  Code is here so once a counter starter ports, the
     * draw fires automatically. */
    if (/* DAT_0438b1b0 == 0 && */ sim_get_counter_994() > 0) {
        const int32_t c994 = sim_get_counter_994();
        const int32_t t94  = sim_get_threshold94();
        if (t94 <= 0) return;  /* engine has no guard; div-by-zero. */

        /* L21: render_quad_state_setup — 2D state preset. */
        render_quad_state_setup(dev);

        /* L22-31: GetDepthStencilSurface + SetRenderTarget + Release
         * dance.  The engine's frame pipeline can redirect to a temp
         * render target during the walker; this restores the saved
         * RT (DAT_073dfce0) + a freshly-fetched depth surface, then
         * Releases the two saved refs.  Until the redirect-source
         * ports (also part of FUN_0040a765's family), DAT_073dfce0 is
         * NULL, the dance is a no-op AddRef-of-current-depth +
         * SetRenderTarget(NULL, depth) which is actually destructive
         * (SetRenderTarget(NULL) is undefined on D3D8).  We skip the
         * dance entirely today; correctness requires the redirect to
         * be live first.
         *
         * TODO C7-followup: re-enable the dance once FUN_0040a765 (the
         * walker) starts setting DAT_073dfce0 / 4 to a temp RT.
         */
        /* (skipped: GetDepthStencilSurface / SetRenderTarget / Release) */

        /* L32: SetTexture(0, post_tex[counter_99d0]).  The engine
         * stores a small array of post-process textures at
         * DAT_073de648; counter_99d0 picks one.  Both the array and
         * the index are BSS-zero today.  Skip until the source
         * (FUN_0040a765 + the redirect dance) lands.
         *
         * TODO C7-followup: SetTexture(0, g_scene1_post_tex[idx]).
         */

        /* L33-46: alpha computation + full-screen quad.  Formula
         * verbatim from the decomp:
         *
         *   theta = c994 * π / threshold94
         *   alpha = 0xff - ftol(sin(theta))
         *
         * ftol() of sin's [-1, 1] range truncates to {-1, 0, 1}.
         * That's almost certainly wrong on its face — Ghidra ate a
         * scale factor.  Most likely the engine actually computes
         *
         *   alpha = 0xff - (int)(sin(theta) * 0xff)
         *
         * (a half-period bell curve from 0xff → 0 → 0xff as the
         * counter sweeps 0..threshold).  We implement the engine's
         * literal form; the side-by-side smoke after a starter ports
         * will show whether the scale factor matters. */
        float theta  = ((float)c994 * 3.1415927f) / (float)t94;
        float s      = sinf(theta);
        int   alpha8 = (int)s;            /* engine's literal ftol */
        alpha8 = 0xff - alpha8;
        if (alpha8 < 0)   alpha8 = 0;
        if (alpha8 > 255) alpha8 = 255;

        /* L34-45: src rect = (0, 0, 640, 480), dst rect = (0, 0, 640,
         * 480) — fullscreen.  Color = alpha << 24 | 0xffffff
         * (white-tinted with the computed alpha). */
        const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        const uint32_t color = ((uint32_t)alpha8 << 24) | 0x00ffffffu;

        /* TODO C7-followup: the engine binds DAT_073cb900's tex_w/h
         * here.  We don't know the source surface's dimensions yet —
         * use 640×480 as a placeholder.  Once the surface ports the
         * tex_w/h come from the bound texture's IDirect3DSurface8
         * GetDesc, matching render_quad_add's expectations. */
        render_quad_add(dst, src, 640u, 480u, color);
        render_quad_flush(dev);
    }
}

/* ─── C7h — FUN_00417504 port ───────────────────────────────────────── */

void scene1_render_overlay(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L6: HUD camera+proj setup (O.11). */
    scene1_overlay_setup(dev);

    /* L7-15: per-frame render-state reset for the 2D RHW + alpha
     * overlay pass.  Engine writes these every frame because the
     * walker leaves the device in scene-1-3D state (lighting on, Z on,
     * fixed-function FVF 0x152).  The overlay needs RHW + diffuse +
     * single-tex, no Z, no lighting. */

    /* L7: D3DRS_COLORVERTEX off — vertices in this pass have no
     * normal, so per-vertex color must come from DIFFUSE directly. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_COLORVERTEX, FALSE);

    /* L8: SetVertexShader with FVF 0x142 = XYZRHW (0x4) | DIFFUSE
     * (0x40) | TEX1 stride-2 (0x100). */
    IDirect3DDevice8_SetVertexShader(dev, 0x142);

    /* L9-10: depth off — UI layers don't interact with the world Z. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);

    /* L11-12: linear texture sampling.  Engine writes 0x11/0x10
     * (= MINFILTER/MAGFILTER) — earlier this port wrote
     * TEXCOORDINDEX/TEXTURETRANSFORMFLAGS due to a dec/hex confusion
     * (`0x11` = 17 = MINFILTER, NOT decimal 11 = TEXCOORDINDEX).
     * Fixed 2026-05-26 alongside the L897 COLORARG2=SPECULAR removal. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

    /* L13: fog off. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* L14: alpha blend on. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);

    /* L15: alpha op modulate (engine asm 0x4175b2 — SetTSS(0, 4, 4)).
     * Was missing from this port; surfaced 2026-05-26 alongside the
     * L897 phantom-write fix. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

    /* L16: ambient — very dim gray (sets a baseline so lighting-off
     * still has some "self emission" feel; the engine uses this for
     * the dimmed-shop atmosphere). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff101010u);

    /* L17: shademode gouraud (per-vertex). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

    /* L18: color op modulate (engine asm 0x4175ec — SetTSS(0, 1, 4)). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    /* NOTE: prior version of this port had an extra
     * SetTSS(0, COLORARG2, D3DTA_SPECULAR) write here that does NOT
     * exist in engine asm (verified via objdump @ 0x417504 — only 4
     * SetTSS calls total, none with state==3=COLORARG2).  That phantom
     * write was the cause of PHC #18 / Cr.2 — the COLORARG2 leak that
     * blanked the "Now Loading" CD icon when scene1_render_overlay was
     * wired.  Removed 2026-05-26. */

    /* L19-20: lighting off (FFP off, vertex diffuse is final). */
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);

    /* L21: cull off (2D quads can be wound either way). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    /* ─── layer 1 — alpha pass ──────────────────────────────────── */
    /* L22-23: SRCBLEND=SRCALPHA, DESTBLEND=INVSRCCOLOR.  Engine quirk
     * — see header note.  Standard alpha would use INVSRCALPHA (6). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    /* L24: FUN_00414ee2(1, 1) — layer 1 dispatch. */
    scene1_overlay_render(dev, /*layer=*/1, /*mode=*/1);

    /* ─── layer 0 — additive pass ──────────────────────────────── */
    /* L25-26: SRCBLEND=ONE, DESTBLEND=ONE (additive). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    /* L27: FUN_00414ee2(0, 1) — layer 0 dispatch. */
    scene1_overlay_render(dev, /*layer=*/0, /*mode=*/1);

    /* ─── layer 2 — alpha pass with alpha-ref 0 ────────────────── */
    /* L28: ALPHAREF = 0 — alpha-test threshold (only matters if
     * ALPHATESTENABLE is on; we don't set that here, so this is
     * defensive). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);
    /* L29-30: back to SRCALPHA / INVSRCCOLOR. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    /* L31: FUN_00414ee2(2, 1) — layer 2 dispatch. */
    scene1_overlay_render(dev, /*layer=*/2, /*mode=*/1);

    /* ─── layer 3 — mask-by-dest pass ──────────────────────────── */
    /* L32: SRCBLEND=ZERO (with DESTBLEND inherited as INVSRCCOLOR).
     * With src=0 the output = dest * INVSRCCOLOR — basically a
     * tint-by-source-color mask.  Used for screen-dim / vignette
     * effects in the engine. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ZERO);
    /* L33: FUN_00414ee2(3, 1) — layer 3 dispatch. */
    scene1_overlay_render(dev, /*layer=*/3, /*mode=*/1);

    /* L34-35: reset blend pair to layer-1 defaults so any code that
     * runs after us (the frame's tail / next frame's pre-walker)
     * inherits the SRCALPHA / INVSRCCOLOR pair.  The engine performs
     * this reset; ports often skip it but we reproduce verbatim. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
}

#endif /* _WIN32 */
