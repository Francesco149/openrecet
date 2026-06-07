/*
 * scene_worldmap.c — see scene_worldmap.h.
 *
 * Engine sources:
 *   FUN_004735ad @ 0x4735ad (98 bytes)  — the 4 fixed BMP/TGA loads.
 *   FUN_0049de20 @ 0x49de20 (374 bytes) — the mode-8 scene-init state
 *     machine (destination model + tutorial gating). Ported here as
 *     scene_worldmap_init_state. Strings extracted via
 *       tools/analyze/pe.py str 0x005c87f4 0x005c880c 0x005c8824 0x005c883c
 *   .data tables extracted from vendor/unpacked @ 0x5fd590 (per-dest
 *     layout) + 0x5fd620 (3×5 cursor grid).
 */

#include "scene_worldmap.h"

#include <stdint.h>

#include "worker_load.h"
#include "scene.h"              /* SCENE_STATE_WORLDMAP (=8) — primary worker case index */
#include "save_work.h"          /* live working save arena (tutorial flags + day/tod) */
#include "save_bank.h"          /* SAVE_BANK_FIELD_CARD_DAY / _CLOCK_TARGET (working dwords) */
#include "title_save_dialog.h"  /* shared cursor: set_visible (FUN_0043561a) + snap (FUN_00435693) */

/* ─── pre-baked asset table ──────────────────────────────────────────── */

const scene_worldmap_asset_t scene_worldmap_assets[SCENE_WORLDMAP_COUNT] = {
    [SCENE_WORLDMAP_TEX_NOMAL]    = { "bmp/worldmap_nomal.bmp",  0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_YUGATA]   = { "bmp/worldmap_yugata.bmp", 0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_NIGHT]    = { "bmp/worldmap_night.bmp",  0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_MAPPOINT] = { "bmp/mappoint.tga",        0x100, 0x400 },
};

const char *scene_worldmap_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_WORLDMAP_COUNT) return 0;
    return scene_worldmap_assets[slot].path;
}

int scene_worldmap_dims(int slot, uint32_t *w, uint32_t *h)
{
    if (slot < 0 || slot >= SCENE_WORLDMAP_COUNT) {
        if (w) *w = 0;
        if (h) *h = 0;
        return 0;
    }
    if (w) *w = scene_worldmap_assets[slot].expected_w;
    if (h) *h = scene_worldmap_assets[slot].expected_h;
    return 1;
}

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_worldmap_load_with(scene_worldmap_load_fn load_fn,
                             void *userdata)
{
    /* Engine FUN_004735ad: 4 unconditional sprite_load calls in fixed
     * order. The first arg (kind = 10) is the engine's per-scene
     * texture-group flag, which openrecet's sprite_load drops. */
    int loads = 0;
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        const scene_worldmap_asset_t *a = &scene_worldmap_assets[i];
        if (load_fn) {
            load_fn(a->path, a->expected_w, a->expected_h, i, userdata);
        }
        loads++;
    }
    return loads;
}

/* ─── world-map destination model + scene-init (FUN_0049de20) ─────────────
 *
 * The mode-8 (WORLD MAP) scene globals + the .data layout tables. Pure-C —
 * the init runs on the worker thread for mode 8 (before the texture load).
 * See docs/findings/town-map-RE.md §3-4. */

/* DAT_005fd590 (per-dest {x,y,sprite_row}, stride 0xc, 7 entries). */
const scene_worldmap_dest_t
    scene_worldmap_dest_layout[SCENE_WORLDMAP_DEST_COUNT] = {
    /*   x       y     row */
    { 230.0f, 400.0f, 0 },  /* dest 0 — Shop / home (your shop) */
    {  43.0f, 294.0f, 3 },  /* dest 1 */
    { 230.0f, 276.0f, 1 },  /* dest 2 */
    { 440.0f, 276.0f, 5 },  /* dest 3 — Market (tutorial-forced target) */
    {  30.0f, 196.0f, 4 },  /* dest 4 */
    { 160.0f, 136.0f, 2 },  /* dest 5 */
    { 448.0f,  88.0f, 6 },  /* dest 6 */
};

/* DAT_005fd620 — 3 col × 5 row cursor grid (col + row*3 → dest-id, -1 = empty). */
const int scene_worldmap_grid[SCENE_WORLDMAP_GRID_COLS *
                              SCENE_WORLDMAP_GRID_ROWS] = {
    -1, -1, -1,
     4,  5,  6,
     1,  2,  3,
     1,  0,  3,
     0, -1,  0,
};

/* Engine world-map scene globals (DAT_096435xx / DAT_005fd588). */
static int   s_dest_count    = SCENE_WORLDMAP_DEST_COUNT; /* DAT_005fd588 */
static int   s_sel_dest      = 0;     /* DAT_09643684 — selected dest (cursor) */
static float s_entry_timer   = 0.0f;  /* _DAT_09643628 — intro timer */
static int   s_exit_counter  = 0;     /* DAT_0964367c  — exit-in-progress */
static int   s_misc_680      = 0;     /* _DAT_09643680 */
static int   s_dest_pos[SCENE_WORLDMAP_DEST_COUNT]    = { 0 }; /* DAT_096435d8 */
static int   s_dest_state[SCENE_WORLDMAP_DEST_COUNT]  = { 0 }; /* DAT_09643588 */
static int   s_dest_closed[SCENE_WORLDMAP_DEST_COUNT] = { 0 }; /* DAT_0964362c */

void  scene_worldmap_set_sel_dest(int dest) { s_sel_dest = dest; }  /* FUN_0049de0e */
int   scene_worldmap_sel_dest(void)         { return s_sel_dest; }
int   scene_worldmap_dest_count(void)       { return s_dest_count; }
float scene_worldmap_entry_timer(void)      { return s_entry_timer; }

int scene_worldmap_dest_state(int i)
{
    return (i >= 0 && i < SCENE_WORLDMAP_DEST_COUNT) ? s_dest_state[i] : 0;
}
int scene_worldmap_dest_closed(int i)
{
    return (i >= 0 && i < SCENE_WORLDMAP_DEST_COUNT) ? s_dest_closed[i] : 0;
}
int scene_worldmap_dest_pos(int i)
{
    return (i >= 0 && i < SCENE_WORLDMAP_DEST_COUNT) ? s_dest_pos[i] : 0;
}

/* Working-arena field locations (base DAT_044e3798, per-slot; see
 * docs/findings/save-working-arena.md). Byte offsets for the BYTE tutorial
 * flags; dword indices for the INT day/tod state. */
#define WM_OFF_TUTORIAL_A  0x2bc61      /* DAT_0450f3f9 — set by the door's first exit */
#define WM_OFF_TUTORIAL_B  0x2bc70      /* DAT_0450f408 */
#define WM_OFF_EVENT_FLAG  0x2bc7c      /* DAT_0450f414 */
#define WM_DW_DAY          SAVE_BANK_FIELD_CARD_DAY    /* 0xb0fb — DAT_0450fb84 (day#) */
#define WM_DW_TOD          SAVE_BANK_FIELD_CLOCK_TARGET/* 0xb0fc — DAT_0450fb88 (0 day/1 eve/2 night) */
#define WM_DW_05A0         0xb3a2u                     /* DAT_045105a0 */

/* PORT-DEBT(event-probe, FUN_0045de68): the per-destination "has an event
 * today" probe reads the event-table arena (DAT_06a49c44, 5000-int strides)
 * + the day/tod gates — the event system is unported. Returns 0 (no event).
 * Only affects ENABLED destinations (state != 0); in the tutorial gate (the
 * town-map-load recording) only dest 3 is enabled and already at state 2, so
 * this is a no-op there. Retire when the event tables port. */
static int scene_worldmap_dest_has_event(int dest)
{
    (void)dest;
    return 0;
}

void scene_worldmap_init_state(void)   /* FUN_0049de20 @ 0x49de20 */
{
    const int slot = save_work_active_slot();           /* DAT_0438b1e0 */
    const uint32_t *dw = save_work_dwords_at(slot);
    const uint8_t  *bb = (const uint8_t *)dw;            /* same per-slot block, byte view */
    int i;

    s_entry_timer  = 0.0f;   /* _DAT_09643628 = 0 */
    s_exit_counter = 0;      /* DAT_0964367c  = 0 */
    s_misc_680     = 0;      /* _DAT_09643680 = 0 */

    /* FUN_0043561a + FUN_00435693: raise the SHARED cursor + snap it to the
     * selected dest's marker (x-16, y+28). This raise is the engine's
     * "PAUSE_OPEN" anchor at the world-map load — the shared b150 cursor for
     * the destination pointer (red herring, town-map-RE.md §1). */
    title_save_dialog_cursor_set_visible(1);
    title_save_dialog_cursor_snap(
        scene_worldmap_dest_layout[s_sel_dest].x - 16.0f,
        scene_worldmap_dest_layout[s_sel_dest].y + 28.0f);

    s_dest_count = SCENE_WORLDMAP_DEST_COUNT;   /* DAT_005fd588 = 7 */
    for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_closed[i] = 0;
    for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_state[i]  = 0;
    for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_pos[i]    = i; /* DAT_096435d8[i]=i */

    if (bb == NULL) return;   /* defensive: no working arena bound */

    /* Tutorial gating (all.c:102868). The door-exit sets flag A on the first
     * exit → the `else` branch → dest 3 (Market) highlighted, the rest
     * disabled. Flag-A-clear + flag-B-clear = all unlocked (NORMAL); flag-B
     * set = only dest 0 highlighted. */
    if (bb[WM_OFF_TUTORIAL_A] == 0) {
        if (bb[WM_OFF_TUTORIAL_B] == 0) {
            for (i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) s_dest_state[i] = 1;
        } else {
            s_dest_state[0] = 2;
        }
    } else {
        s_dest_state[3] = 2;   /* _DAT_09643594 = 2 — Market highlighted */
    }

    {
        const int day = (int)dw[WM_DW_DAY];   /* DAT_0450fb84 */
        const int tod = (int)dw[WM_DW_TOD];   /* DAT_0450fb88 */

        /* Time-of-day / day closures (all.c:102883). */
        if (day % 7 == 3) {
            s_dest_closed[6] = 1;   /* _DAT_09643644 = 1 */
            s_dest_state[6]  = 0;   /* _DAT_096435a0 = 0 */
        }
        if (tod < 3 && (tod < 2 || (int)dw[WM_DW_05A0] == 0)) {
            if (bb[WM_OFF_EVENT_FLAG] != 0) s_dest_state[6] = 2; /* _DAT_096435a0 = 2 */
        } else {
            s_dest_state[6] = 0;
        }
        if (tod == 3) s_dest_state[1] = 0;   /* DAT_0964358c  = 0 */
        if (tod <  2) s_dest_state[4] = 0;   /* _DAT_09643598 = 0 */
        if (tod == 3) s_dest_state[5] = 0;   /* _DAT_0964359c = 0 */
    }

    /* Event upgrade (all.c:102905): dest 1..5 — an enabled dest with an event
     * today becomes highlighted. PORT-DEBT event probe → no-op for now. */
    for (i = 1; i < 6; i++) {
        if (scene_worldmap_dest_has_event(i) && s_dest_state[i] != 0)
            s_dest_state[i] = 2;
    }

    /* PORT-DEBT(stage-scratch, FUN_00435c98): the engine tail re-inits the
     * INGAME stage/camera scratch + the day-display float (_DAT_0438b91c).
     * Deferred — it runs AFTER the destination state array is set, so it has
     * no effect on the model; revisit if the world-map render (T3) needs the
     * camera / day-display globals. */
}

/* Per-frame update — mode-8 dispatch (sim.c case 8). The engine mode-8 update
 * (FUN_004536cb LAB_00453bed) runs the shared FUN_00406584 (cursor bob) +
 * FUN_0040fb3a, then the per-state callee FUN_0049e163 (entry timer + 3×5
 * cursor-nav + Z-select → the destination→mode table). T4 ports that body;
 * for now the map sits idle in mode 8 (T2 only needs to REACH it). */
void scene_worldmap_sim(void)
{
}

/* ─── Win32 worker_load wiring + sprite storage ──────────────────────── */

#ifdef _WIN32

#include <math.h>

#include "render_quad.h"      /* FUN_00404efc/405354/49b425 — quad add/flush/state +
                              * brings in <d3d8.h> with COBJMACROS+CINTERFACE */
#include "font_draw.h"        /* FUN_0047d14c — centred text ("Closed") */
#include "scene1_top_hud.h"   /* scene1_top_hud_clock_phase — DAT_0438b7d4 */
#include "sprite.h"

sprite_t g_scene_worldmap[SCENE_WORLDMAP_COUNT];

static IDirect3DDevice8 *g_scene_worldmap_dev = 0;

/* Win32 load_fn: drives sprite_load against the per-slot sprite_t. */
static int win32_load_fn(const char *path,
                         uint32_t w, uint32_t h,
                         int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    /* Engine: FUN_0047193c(10, dst, name, w, h). The kind=10 is the
     * scene-1 texture-group flag; sprite_load drops it (same as the
     * rest of the openrecet call sites). */
    return sprite_load(dev, path, w, h, &g_scene_worldmap[slot]);
}

static void scene_worldmap_body(void)
{
    /* Engine LAB_00452c96's inner-body calls (objdump @ 0x452c96..):
     *     call 0x49de20    ; FUN_0049de20  — world-map state machine
     *     call 0x4735ad    ; FUN_004735ad  — world-map BMP loader  (THIS)
     *
     * This is the SECONDARY C96 body, which has no port-side spawner caller
     * (dormant). The live world-map path is the PRIMARY worker case-8 body
     * below; keep this BMP-only (the dormant half-port). */
    scene_worldmap_load_with(win32_load_fn, g_scene_worldmap_dev);
}

/* PRIMARY worker case-8 body — engine primary jump-table @ 0x452984:
 *     call 0x49de20    ; FUN_0049de20  — world-map scene init
 *     call 0x4735ad    ; FUN_004735ad  — world-map BMP loader
 * The door-exit stage-2 (scene1_player_ctrl.c) sets g_scene_state = 8 +
 * spawns the primary worker; worker_load_thread_proc dispatches it here. */
static void scene_worldmap_primary_cb(void)
{
    scene_worldmap_init_state();
    scene_worldmap_load_with(win32_load_fn, g_scene_worldmap_dev);
}

void scene_worldmap_init(struct IDirect3DDevice8 *dev)
{
    g_scene_worldmap_dev = (IDirect3DDevice8 *)dev;
    /* Secondary C96 body (BMP-only, dormant — no spawner caller yet). */
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C96, scene_worldmap_body);
    /* PRIMARY case-8 body (init + load) — the live door→world-map path. */
    worker_load_set_cb(SCENE_STATE_WORLDMAP, scene_worldmap_primary_cb);
}

/* Per-frame render — mode-8 render dispatch (main.c render switch case 8).
 *
 * Port of FUN_0049e3a3(scale) @ 0x49e3a3 (the body of the mode-8 render
 * wrapper FUN_0049e686 → FUN_0049e3a3(1.0)). Three layers (all.c:103136):
 *   1. the worldmap photo with a time-of-day crossfade (2 passes),
 *   2. the mappoint.tga destination markers (per-state alpha/size + the
 *      selected dest drawn bigger), under COLOROP=ADDSIGNED,
 *   3. centred red "Closed" labels for closed destinations.
 *
 * `scale` is always 1.0 (the wrapper hard-codes it), so the bg dst is
 * 640x480 in 640-relative coords — render_quad_add then scales by
 * screen_w/640 like the engine's FUN_00404efc. The engine wrapper also
 * Clears to cyan first; the port relies on main.c's per-frame Clear (the
 * opaque bg covers the whole framebuffer, so the clear colour is moot).
 *
 * NOTE: the engine dispatch is `FUN_0049e686(); FUN_0040a765();` — the
 * world map also draws the full INGAME HUD aggregator (top clock/Day/money
 * + the tutorial text box). That is a separate, larger shared function
 * (FUN_0040a765); wiring it for mode 8 is a follow-up chip. T3 is the
 * worldmap scene body only. The trailing COLOROP reset below restores
 * MODULATE precisely so that HUD (when wired) draws under the right op. */
void scene_worldmap_render(struct IDirect3DDevice8 *dev)
{
    /* FUN_0049b425 — 2D render-state preset (alpha blend, COLOROP=MODULATE,
     * linear filter). The bg crossfade passes draw under MODULATE. */
    render_quad_state_setup(dev);

    /* ── 1. background: time-of-day crossfade (all.c:103162) ──────────────
     * tod = working dword 0xb0fc, used RAW (1=day / 2=eve / 3=night → the
     * texture indices land on worldmap_nomal/yugata/night via tod-1/tod-2).
     *   pass 0 (base):  worldmap[max(tod-2,0)] @ alpha 0xff
     *   pass 1 (blend): worldmap[max(tod-1,0)] @ alpha 0xff-ftol((tod-clock)*255)
     * clock = DAT_0438b7d4 (the animated time-of-day float; on a CONTINUE it
     * is snapped to tod, so the blend term is ~0 and pass 1 covers pass 0). */
    const int       slot  = save_work_active_slot();         /* DAT_0438b1e0 */
    const uint32_t *dw    = save_work_dwords_at(slot);
    const int       tod   = dw ? (int)dw[WM_DW_TOD] : 0;     /* DAT_0450fb88 */
    const float     clock = scene1_top_hud_clock_phase();    /* DAT_0438b7d4 */

    const float bg_dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };  /* scale(=1)*640/480 */
    const float bg_src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };  /* top-left of the 1024x512 bmp */

    for (int pass = 0; pass < 2; pass++) {
        int alpha = 0xff;
        int tex_idx;
        if (pass == 1) {
            /* 0x519630 = 255.0; FUN_00503954 = __ftol (truncate toward 0). */
            alpha   = 0xff - (int)(((float)tod - clock) * 255.0f);
            tex_idx = tod - 1;
        } else {
            tex_idx = tod - 2;
        }
        if (tex_idx < 0) tex_idx = 0;

        render_quad_bind(dev, &g_scene_worldmap[tex_idx]);
        render_quad_add(bg_dst, bg_src,
                        g_scene_worldmap[tex_idx].width,
                        g_scene_worldmap[tex_idx].height,
                        ((uint32_t)alpha << 24) | 0x00ffffffu);
        render_quad_flush(dev);
    }

    /* ── 2. destination markers (mappoint.tga, all.c:103193) ──────────────
     * Engine sets COLOROP=ADDSIGNED (8) before the markers and holds it
     * through the "Closed" labels, resetting to MODULATE (4) at the end. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

    const sprite_t *mp = &g_scene_worldmap[SCENE_WORLDMAP_TEX_MAPPOINT];
    render_quad_bind(dev, mp);

    const float timer = scene_worldmap_entry_timer();   /* _DAT_09643628 (T4 sim: frozen→0) */
    const int   sel   = scene_worldmap_sel_dest();      /* DAT_09643684 */
    const int   n     = scene_worldmap_dest_count();    /* DAT_005fd588 */

    for (int i = 0; i < n; i++) {
        const int pos   = scene_worldmap_dest_pos(i);     /* DAT_096435d8[i] (map-pos) */
        const int state = scene_worldmap_dest_state(i);   /* DAT_09643588[i] */

        float w = 144.0f, h = 44.8f;   /* default marker size */
        int   size_alpha = 200;        /* iVar4 — ARGB alpha channel */
        int   grey       = 0x7f;       /* uVar1 — RGB (ADDSIGNED brightness) */
        if (state == 0) grey = 0x40;                                /* disabled: dim */
        if (state == 2)                                             /* highlighted: sin-pulse */
            grey = (int)(sinf(timer * 0.15f) * 16.0f + 143.0f);     /* FUN_00503a44 sinf + __ftol */
        if (pos == sel) { size_alpha = 0xff; w = 180.0f; h = 56.0f; } /* selected: bigger + opaque */

        const scene_worldmap_dest_t *L = &scene_worldmap_dest_layout[pos];
        const float row_top = (float)(L->sprite_row * 0x38);        /* 56 px tall rows */
        const float src[4] = { 0.0f, row_top, 180.0f, (float)((L->sprite_row + 1) * 0x38) };
        const float dst[4] = {
            (L->x + 90.0f) - w * 0.5f,
            (L->y + 28.0f) - h * 0.5f,
            w, h,
        };
        /* ARGB(size_alpha, grey, grey, grey) — engine's nested shift-or. */
        const uint32_t col =
            ((((uint32_t)size_alpha << 8 | (uint32_t)grey) << 8
              | (uint32_t)grey) << 8) | (uint32_t)grey;
        render_quad_add(dst, src, mp->width, mp->height, col);
    }
    render_quad_flush(dev);

    /* ── 3. "Closed" labels (all.c:103233) — centred red, scale 1.2 ──────── */
    for (int i = 0; i < n; i++) {
        if (scene_worldmap_dest_closed(i)) {
            const scene_worldmap_dest_t *L =
                &scene_worldmap_dest_layout[scene_worldmap_dest_pos(i)];
            font_draw_text_centered(dev, L->x + 94.0f, L->y + 16.0f,
                                    "Closed", 0xffff3737u, 1.2f);   /* s_Closed_005fd65c */
        }
    }
    render_quad_flush(dev);

    /* COLOROP reset → MODULATE (engine FUN_0049e3a3 tail, all.c:103244) so
     * the trailing HUD aggregator (and the next frame) draws under MODULATE. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

#endif /* _WIN32 */

/* ─── reset (pure-C model + Win32 sprites) ───────────────────────────── */

void scene_worldmap_reset(void)
{
    /* Pure-C destination model. */
    s_dest_count   = SCENE_WORLDMAP_DEST_COUNT;
    s_sel_dest     = 0;
    s_entry_timer  = 0.0f;
    s_exit_counter = 0;
    s_misc_680     = 0;
    for (int i = 0; i < SCENE_WORLDMAP_DEST_COUNT; i++) {
        s_dest_pos[i]    = 0;
        s_dest_state[i]  = 0;
        s_dest_closed[i] = 0;
    }
#ifdef _WIN32
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        /* Same lazy-reset shape as scene_floor/jutan — leaves the
         * IDirect3D resource leaked, which is fine because tests on
         * Win32 don't run sprite_load, and the engine's lost-device
         * handling (not yet ported) will own the real teardown. */
        g_scene_worldmap[i].tex    = 0;
        g_scene_worldmap[i].width  = 0;
        g_scene_worldmap[i].height = 0;
    }
#endif
}
