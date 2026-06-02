/*
 * scene1_hud.c — port of FUN_0040a765 (0x40a765, 7558 B), the scene-1
 * 2D HUD / overlay aggregator.  See scene1_hud.h for the pass map.
 *
 * C7j chip: entry shell + Pass 1 (stamina/HP backdrop, DUNGEON-only) +
 * Pass 2 (letterbox bars) + Pass 3 (status-screen takeover).  Every
 * pass past Pass 1's state preset is gated on a BSS-zero global in
 * HOUSE, so the wired shell is visually inert there — it lands the
 * structure + the 2D render-state preset the later passes inherit.
 *
 * Engine globals owned / referenced by this chip:
 *   DAT_005c570c   master render gate (.data = 1, const) → never early-
 *                  returns in practice; reproduced for fidelity.
 *   DAT_0438b1c0   scene mode (1 = INGAME)            → g_scene_state.
 *   *DAT_068dd2f0  stage type (0 = HOUSE, >0 DUNGEON) → stage record
 *                  field 0 (maptype) via scene1_current_stage_record().
 *   DAT_0438b1e0   save-slot index (Pass 4+ per-slot base; computed
 *                  here only as the engine does, unused until C7l).
 *   DAT_0438b1dc   letterbox-bar height (Pass 2)      → module-local.
 *   DAT_073dddb4   status-screen-open flag (Pass 3)   → module-local.
 *   DAT_056db104   Pass-1 backdrop alt-gate           → module-local.
 *   DAT_073cc8f0   shade.bmp  (Pass 1 backdrop)       → g_sysassets.shade_bmp.
 *   DAT_073aa188   system.bmp (Pass 2 bars)           → g_sysassets.system_bmp.
 *
 * Deferred sub-calls (stubbed, faithful call-count):
 *   FUN_0049065b (314 B) — Pass-1 sub-init: snapshots a 32-dword block
 *                          (DAT_073de29c..) into the DAT_095d37xx 2D-
 *                          overlay-camera arrays.  Source block + the
 *                          consumer subsystem are BSS-zero / unported,
 *                          so the call is a no-op on the HOUSE path.
 *   FUN_004141c0 (389 B) — Pass-3 status-screen render.  Reachable only
 *                          when DAT_073dddb4 != 0 (Q-menu open).
 *   FUN_0043647f          — Pass-1 dungeon backdrop predicate; only
 *                          evaluated inside the (dormant) DUNGEON gate.
 */

#include "scene1_hud.h"

/* ─── module-local engine globals (no other owner yet) ─────────────── */

/* DAT_0438b1dc — letterbox-bar height (float).  Writer is the cinema-
 * bars animator (unported); default 0 keeps Pass 2 dormant. */
static float g_hud_letterbox_h = 0.0f;

/* DAT_073dddb4 — status-screen-open flag.  Set to 1 only while the
 * in-shop status/Q-menu is up (engine all.c:73798); 0 otherwise. */
static int g_hud_status_screen = 0;

/* DAT_056db104 — Pass-1 stamina-backdrop alternate gate.  BSS-zero;
 * only consulted on the DUNGEON path. */
static int g_hud_dat_056db104 = 0;

/* ─── pure helpers (host-testable) ─────────────────────────────────── */

float scene1_hud_letterbox_height(void)
{
    /* Engine: `if (-0.1 <= h && h <= 0.1) h = 0;` — a ±0.1 dead-zone
     * so the bars stay fully retracted until the animator pushes the
     * height clear of zero. */
    float h = g_hud_letterbox_h;
    if (h >= -0.1f && h <= 0.1f)
        h = 0.0f;
    return h;
}

void scene1_hud_set_letterbox_height(float h) { g_hud_letterbox_h = h; }

int  scene1_hud_status_screen_open(void) { return g_hud_status_screen; }
void scene1_hud_set_status_screen_open(int open) { g_hud_status_screen = open ? 1 : 0; }

uint32_t scene1_hud_pass1_backdrop_color(int pred)
{
    /* uVar7 = pred ? 0xff : 200; engine packs it as
     * `((uVar7 | 0x3700) << 8 | uVar7) << 8 | uVar7` — RGB = (uVar7)³,
     * alpha byte forced to 0x37. */
    uint32_t a = pred ? 0xffu : 200u;
    return (uint32_t)((((a | 0x3700u) << 8) | a) << 8) | a;
}

int scene1_hud_pass1_backdrop_active(int scene_mode, int stage_type, int pred)
{
    return (scene_mode == 1) && (stage_type > 0) &&
           (pred != 0 || g_hud_dat_056db104 != 0);
}

/* ─── Win32 render body ────────────────────────────────────────────── */

#ifdef _WIN32

#include "render_quad.h"
#include "sysassets.h"
#include "scene.h"            /* g_scene_state (DAT_0438b1c0) */
#include "scene1_maplight.h"  /* scene1_current_stage_record (DAT_068dd2f0) */
#include "scene1_top_hud.h"   /* FUN_00406d50 — persistent clock/day/money HUD */
#include "scene1_merchant_hud.h"  /* FUN_00409925 body — bottom-left Merchant Level HUD */
#include "call_trace.h"

/* FUN_0049065b — Pass-1 sub-init (deferred; see header comment). */
static void scene1_hud_subinit_TODO(void)
{
    CALL_TRACE_ENTER_STUB(0x49065bu);
}

/* FUN_004141c0 — Pass-3 status-screen render (deferred). */
static void scene1_hud_status_screen_render_TODO(void)
{
    CALL_TRACE_ENTER_STUB(0x4141c0u);
}

/* FUN_0043647f(0x10) — Pass-1 DUNGEON backdrop predicate (deferred).
 * Only evaluated inside the DUNGEON gate, dormant in HOUSE. */
static int scene1_hud_fun_0043647f(int flag)
{
    (void)flag;
    return 0;
}

/* *DAT_068dd2f0 + 0 — stage type (0 = HOUSE, >0 = DUNGEON). */
static int scene1_hud_stage_type(void)
{
    const stage_record_t *rec = scene1_current_stage_record();
    return rec ? rec->maptype : 0;
}

void scene1_hud_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* E.2 probe — FUN_0040a765 @ 0x40a765. */
    CALL_TRACE_ENTER(0x40a765u);

    /* L45 — per-save-slot record base
     *   local_48 = &DAT_044e3798 + DAT_0438b1e0 * 0x2dfc8
     * is computed here in the engine but only consumed by Pass 4+
     * (speech bubbles, sub-menu panels).  Deferred with those passes;
     * no read happens before then. */

    /* L46 — Pass-1 sub-init (DAT_095d37xx 2D-overlay camera feed). */
    scene1_hud_subinit_TODO();

    /* L47 — master render gate (DAT_005c570c).  .data-initialised to 1
     * and never written elsewhere, so this never returns in practice;
     * reproduced for fidelity. */
    /* DAT_005c570c == 1 → proceed. */

    /* L48 — 2D render-state preset (FUN_0049b425). */
    render_quad_state_setup(dev);

    int scene_mode = g_scene_state;
    int stage_type = scene1_hud_stage_type();

    /* ── Pass 1 — stamina/HP backdrop (L49-73) ─────────────────────────
     * Gate: INGAME && DUNGEON && (FUN_0043647f(0x10) || DAT_056db104).
     * DUNGEON-only → dormant in HOUSE.  Full-screen 640x480 mask quad
     * over shade.bmp with SRCBLEND=ZERO / DESTBLEND=SRCCOLOR (dim the
     * backdrop by the sprite's per-pixel colour), then restore the
     * default SRCALPHA / INVSRCALPHA pair. */
    {
        int pred = 0;
        if (scene_mode == 1 && stage_type > 0) {
            pred = scene1_hud_fun_0043647f(0x10);
            if (scene1_hud_pass1_backdrop_active(scene_mode, stage_type, pred)) {
                const sprite_t *shade = &g_sysassets.shade_bmp;
                if (shade->tex) {
                    IDirect3DDevice8_SetTexture(dev, 0,
                        (IDirect3DBaseTexture8 *)shade->tex);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ZERO);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);

                    const float dst[4] = {   0.0f,   0.0f, 640.0f, 480.0f };
                    const float src[4] = {  64.0f,   0.0f, 192.0f, 128.0f };
                    render_quad_add(dst, src, shade->width, shade->height,
                                    scene1_hud_pass1_backdrop_color(pred));
                    render_quad_flush(dev);

                    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
                    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                }
            }
        }
    }

    /* ── Pass 2 — letterbox / cinema bars (L74-100) ────────────────────
     * Height keyed off DAT_0438b1dc (±0.1 dead-zone).  Two 640-wide
     * bars sampling a 7x7 patch of system.bmp: top at y=0 and bottom
     * flush against the screen edge.  Dormant (DAT_0438b1dc BSS-zero). */
    {
        float bar_h = scene1_hud_letterbox_height();
        if (bar_h > 0.0f) {
            const sprite_t *sys = &g_sysassets.system_bmp;
            if (sys->tex) {
                float h = bar_h * 32.0f;
                IDirect3DDevice8_SetTexture(dev, 0,
                    (IDirect3DBaseTexture8 *)sys->tex);

                const float src[4] = { 0.0f, 0.0f, 7.0f, 7.0f };
                /* top bar: dst {0, 0, 640, h} */
                const float dst_top[4] = { 0.0f, 0.0f, 640.0f, h };
                render_quad_add(dst_top, src, sys->width, sys->height, 0xffffffffu);
                /* bottom bar: dst {0, 480-h, 640, h} */
                const float dst_bot[4] = { 0.0f, 480.0f - h, 640.0f, h };
                render_quad_add(dst_bot, src, sys->width, sys->height, 0xffffffffu);
                render_quad_flush(dev);
            }
        }
    }

    /* ── Pass 3 — status-screen takeover (L101-104) ────────────────────
     * When the in-shop status/Q-menu is open the HUD aggregator renders
     * that screen and returns before every later pass.  Dormant in
     * normal HOUSE play (DAT_073dddb4 == 0). */
    if (scene1_hud_status_screen_open()) {
        scene1_hud_status_screen_render_TODO();
        return;
    }

    /* Passes 4-9 (item tooltip, HOUSE/DUNGEON sub-walkers, speech
     * bubbles, shop terminal, chr render, dialog/sub-menu panels,
     * day-counter flash) land as later chips (C7k..C7p).  Most are
     * dormant in free-roam (shop/event-state gated).
     *
     * The one persistent member is FUN_00406d50 (decomp L6980): the
     * gold clock dial + rotating hand + "Day N" badge + money banner.
     * It is drawn unconditionally by the aggregator (the dormant passes
     * above it draw nothing), so we call it here at the equivalent
     * point. */

    /* FUN_00409925 (the HOUSE-town HUD), in engine order:
     *   - body L124-L179: the bottom-left "Merchant Level" badge + XP bar
     *     (scene1_merchant_hud_render); always drawn here.
     *   - tail LAB_0040a5fd: the bottom-right "Button 4: Change Camera" hint
     *     (scene1_top_hud_camera_hint); self-gates on no-dialogue-active.
     * The leading item-tooltip block and the trailing shop/stocking UI are
     * event/shop-state gated and dormant in free-roam.  FUN_00406d50 (the top
     * HUD) is emitted later by the aggregator, so it draws last. */
    scene1_merchant_hud_render(dev_in);
    scene1_top_hud_camera_hint(dev_in);

    scene1_top_hud_render(dev_in);
}

#endif /* _WIN32 */
