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

#include "math3d.h"
#include "mesh_draw.h"
#include "render_quad.h"
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

/* FUN_00441c3e (2217 B) — camera-pose update.  Writes DAT_073de29c.
 * Until this lands, the view matrix stays at whatever the most recent
 * scene1_render_reset_view / scene1_render_view_matrix writer left
 * (identity at boot). */
static void scene1_camera_pose_TODO(void)
{
    /* TODO C7-followup: port FUN_00441c3e.  Reads several player-pose
     * globals (DAT_073de31c..330 for position, DAT_0438cd78/cdb8 for
     * Euler angles), composes a D3DXMatrixRotation+Translation, writes
     * the result into g_scene1_view. */
}

/* FUN_004424e7 (429 B) — scene-angle / orientation update.  Sibling
 * of the camera-pose helper.  Reads DAT_073de31c..330 (player +
 * camera positions), computes pitch/yaw from differences, writes
 * DAT_0438cd78 / cdb8 + recomputes DAT_0438bfa8 (a sub-frame phase
 * counter). */
static void scene1_camera_angle_TODO(void)
{
    /* TODO C7-followup: port FUN_004424e7.  Outputs feed the camera-
     * pose helper above + the FX tail's sin-shake formula. */
}

/* FUN_00454191 (1391 B) — screen-effect overlays (per-counter shake +
 * flash + per-stage dim).  Reads DAT_06a49990 / 06a49994 / 06a4999c
 * and emits up to three full-screen alpha quads.  All three counter
 * starters (FUN_004532b1, FUN_004532bc, scene-transition flashes) are
 * unported, so the function is a no-op against BSS-zero counter
 * state. */
static void scene1_fx_overlays_TODO(void)
{
    /* TODO C7-followup: port FUN_00454191.  Today nothing reads or
     * writes the counters this function consumes (sim_step_a's
     * pump runs against BSS-zero), so the visible state would be
     * "no overlays" regardless. */
}

/* FUN_00452f58 (491 B) — HUD camera + projection setup for the
 * overlay pass.  Computes a separate D3DXMatrixLookAtRH +
 * D3DXMatrixPerspectiveFovRH (different fov / near / far than the
 * main scene), writes both into the device, plus initializes the
 * post-process texture state DAT_06a47120 / 06a475f0.  Required
 * before the layer dispatcher runs to position the 2D layers
 * correctly. */
static void scene1_overlay_setup_TODO(void)
{
    /* TODO C7-followup: port FUN_00452f58.  Until this lands the
     * overlay pass inherits whatever transforms the walker left;
     * for the layer-1/0/2/3 dispatch (also deferred) this is fine
     * because each FUN_00414ee2 call sets its own transforms. */
}

/* FUN_00414ee2 (4006 B) — per-layer 2D overlay dispatcher.  Called
 * four times with layer ∈ {0, 1, 2, 3} and a constant 1 second arg.
 * Each call walks the corresponding queue (text strings, sprite
 * batches, HUD widgets), emitting render_quad_add calls for each
 * element.  The queues are populated by gameplay code via dozens of
 * small enqueue helpers.  This is the single biggest deferred piece
 * in scene-1 overlay rendering. */
static void scene1_overlay_layer_TODO(int layer)
{
    /* TODO C7-followup: port FUN_00414ee2.  Caller has already set
     * the blend modes appropriate for this layer's pass; the
     * dispatcher just needs to walk the per-layer queue. */
    (void)layer;
}

/* ─── C7f — FUN_0045bbf9 port ───────────────────────────────────────── */

void scene1_render_camera_setup(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L8-10 (45bbf9.c): "no shake / no menu" gate for the camera-
     * pose helper.  counter_998 is sim.c's DAT_06a49998 (cyclic 0..0x14
     * or 1..0xc depending on mode); counter_6fa4 (DAT_06a46fa4) is a
     * separate menu/dialog gate that has no porter yet — stays at 0,
     * which keeps the gate open. */
    if (sim_get_counter_998() == 0 /* && counter_6fa4 == 0 */) {
        scene1_camera_pose_TODO();
    }
    /* L11: unconditional angle update. */
    scene1_camera_angle_TODO();

    /* L12: SetTransform(D3DTS_VIEW, &g_scene1_view).  D3DTS_VIEW is
     * device-vtable index 0x94 / 4 = constant 2; the engine writes the
     * raw 2.  Until the camera helpers above port, this pushes
     * identity. */
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

    /* L15: FUN_00459dfd = mesh_set_default_render_state (mesh_draw.c).
     * Ports the full L86..L198 baseline (cull, depth, lighting,
     * COLORVERTEX routing, ambient, FVF 0x152, sampler defaults). */
    mesh_set_default_render_state(dev);
}

/* ─── C7g — FUN_0045404b port ───────────────────────────────────────── */

void scene1_render_fx_tail(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L19: full screen-effect overlay pass. Deferred — see stub. */
    scene1_fx_overlays_TODO();

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

    /* L6: HUD camera+proj setup. Deferred — see stub. */
    scene1_overlay_setup_TODO();

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

    /* L11-12: TSS stage 0 texcoord routing — index 2, transform
     * flags COUNT2 (read uv from input stream index 2 — wait, FVF only
     * has TEX1, so this is engine over-config). Reproduced verbatim. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_TEXCOORDINDEX,        2);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, 2);

    /* L13: fog off. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* L14: alpha blend on. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);

    /* L15: color op modulate. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    /* L16: ambient — very dim gray (sets a baseline so lighting-off
     * still has some "self emission" feel; the engine uses this for
     * the dimmed-shop atmosphere). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff101010u);

    /* L17: shademode gouraud (per-vertex). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

    /* L18: COLORARG2 = D3DTA_SPECULAR.  Unusual — the secondary color
     * argument normally is TEXTURE or TFACTOR.  D3DTA_SPECULAR pulls
     * from a specular channel that FVF 0x142 doesn't declare, which
     * means the engine is implicitly relying on a 0 (or last-written)
     * value here.  Reproduced verbatim. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_SPECULAR);

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
    scene1_overlay_layer_TODO(1);

    /* ─── layer 0 — additive pass ──────────────────────────────── */
    /* L25-26: SRCBLEND=ONE, DESTBLEND=ONE (additive). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    /* L27: FUN_00414ee2(0, 1) — layer 0 dispatch. */
    scene1_overlay_layer_TODO(0);

    /* ─── layer 2 — alpha pass with alpha-ref 0 ────────────────── */
    /* L28: ALPHAREF = 0 — alpha-test threshold (only matters if
     * ALPHATESTENABLE is on; we don't set that here, so this is
     * defensive). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);
    /* L29-30: back to SRCALPHA / INVSRCCOLOR. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    /* L31: FUN_00414ee2(2, 1) — layer 2 dispatch. */
    scene1_overlay_layer_TODO(2);

    /* ─── layer 3 — mask-by-dest pass ──────────────────────────── */
    /* L32: SRCBLEND=ZERO (with DESTBLEND inherited as INVSRCCOLOR).
     * With src=0 the output = dest * INVSRCCOLOR — basically a
     * tint-by-source-color mask.  Used for screen-dim / vignette
     * effects in the engine. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ZERO);
    /* L33: FUN_00414ee2(3, 1) — layer 3 dispatch. */
    scene1_overlay_layer_TODO(3);

    /* L34-35: reset blend pair to layer-1 defaults so any code that
     * runs after us (the frame's tail / next frame's pre-walker)
     * inherits the SRCALPHA / INVSRCCOLOR pair.  The engine performs
     * this reset; ports often skip it but we reproduce verbatim. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
}

#endif /* _WIN32 */
