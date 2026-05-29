/*
 * scene1_preload.c — see scene1_preload.h for the chip writeup.
 *
 * Engine FUN_00474a9a HOUSE branch (the *DAT_068dd2f0 == 0 arm).
 */

#include "scene1_preload.h"

#include "call_trace.h"
#include "mesh_load.h"
#include "scene1_camera.h"   /* scene1_camera_apply_house_groundtruth (bias stand-in) */
#include "scene1_postload.h"
#include "scene1_records.h"
#include "stage_palette.h"
#include "stage_state.h"
#include "worker_load.h"

/*
 * The 21-entry chr portrait id table at .rdata 0x5c8058..0x5c80ac.
 * Each entry is a chr index used to compute "bmp/chr/chr_%02d.bmp"
 * and as the row-key into the per-chr record array DAT_0438cec8
 * (stride 0x1416, width/height at +0/+4). Dump matches the engine
 * byte-for-byte (verified via tools/analyze/pe.py).
 */
const int32_t g_scene1_chr_portrait_ids[SCENE1_PRELOAD_CHR_PORTRAIT_COUNT] = {
    10, 35, 29, 28, 32, 39, 36, 37, 38, 30, 31,
    40, 41, 42, 43,  4,  5, 47, 48, 66, 67,
};

/* C8e.smoke post-house callback — see scene1_preload.h.  Default NULL
 * → no-op.  Set non-NULL by main.c when --force-pass-d-mesh needs to
 * re-load its mesh after mesh_tex_cache_reset().  Storage + setter
 * live outside #ifdef _WIN32 so host tests can exercise the contract
 * without linking <d3d8.h>.  The call site (in scene1_preload_house)
 * is Win32-only. */
static void (*g_scene1_preload_post_house_cb)(void) = NULL;

void scene1_preload_set_post_house_callback(void (*cb)(void))
{
    g_scene1_preload_post_house_cb = cb;
}

void (*scene1_preload_get_post_house_callback(void))(void)
{
    return g_scene1_preload_post_house_cb;
}

#ifdef _WIN32

#include <d3d8.h>
#include <stdio.h>

#include "scene_floor.h"
#include "scene_jutan.h"
#include "scene_map_meshes.h"
#include "scene_table.h"
#include "scene_walls.h"
#include "scene1_walker_pass_init.h"
#include "sprite.h"

/* PII.3c texture-hook adapters — bind the selector-matched stage
 * texture for draw loop A's kabe (wall) / yuka (floor) / jutan (rug)
 * material classes.  Engine: the per-cache-slot SetTexture dispatch
 * (decomp L52813-L52870) picks these per-stage textures rather than
 * the mesh's embedded TextureFilename.  scene_walls/floor/jutan have
 * already sprite_load'd the selector-matched .bmp into
 * g_scene_X[selector]; without these hooks the slot binds NULL and
 * the floor/walls/rug render as untextured grey. */
static void *house_kabe_texture(void)
{
    int s = g_scene_walls_selector;
    if (s < 0 || s >= SCENE_WALLS_COUNT) return NULL;
    return g_scene_walls[s].tex;
}
static void *house_yuka_texture(void)
{
    int s = g_scene_floor_selector;
    if (s < 0 || s >= SCENE_FLOOR_COUNT) return NULL;
    return g_scene_floor[s].tex;
}
static void *house_jutan_texture(void)
{
    int s = g_scene_jutan_selector;
    if (s < 0 || s >= SCENE_JUTAN_COUNT) return NULL;
    return g_scene_jutan[s].tex;
}

/* Storage for the 2 fixed singletons + 21 chr portraits. Engine has
 * these at named globals (leve_win @ DAT_073d9ff0, mood_para @
 * DAT_073d8658, chr portraits @ DAT_073a9b18 stride 0x10). We keep
 * private arrays; readers wire to these by name when their consumers
 * port. */
static sprite_t g_scene1_leve_win;
static sprite_t g_scene1_mood_para;
static sprite_t g_scene1_chr_portraits[SCENE1_PRELOAD_CHR_PORTRAIT_COUNT];

/* HIKARI/WATER animated-texture hook (engine DAT_073aa198[frame]).  The
 * engine's HOUSE asset loader (FUN_00474a9a L73104-73113) loads the
 * per-stage hikari frames "<prefix><NN>.bmp" into DAT_073aa198, and the
 * hikari pass (FUN_00457714 param_1==3) binds DAT_073aa198[anim_frame]
 * via SetTexture.
 *
 * GROUND TRUTH (D3D state-trace A/B, runs/retail-d3d-house frame 14000 vs
 * runs/port-d3d-house7 frame 3300): for HOUSE the engine binds
 * `SetTexture(0, NULL)` for all 5 hikari god-ray submeshes (start_idx
 * 4497/4521/4761/4833/5919 of shop_1st.x, prim 8/8/24/24/16 — identical on
 * both sides).  i.e. HOUSE's DAT_073aa198[] frame table is EMPTY, so the
 * draws are pure vertex-diffuse × 2 additive glow (COLOROP=MODULATE2X,
 * COLORARG1=DIFFUSE, SRC=ONE/DEST=ONE).  Two earlier wrong bindings:
 *   - the submesh's embedded sprite (xfile/shop/hikari.bmp) → cyan
 *     triangulated curtains;
 *   - `mood_para` (commit 4070c3f) → over-saturated flat-green curtains.
 * Both painted a texture where retail uses none.  `mood_para` is NOT the
 * HOUSE hikari prefix — that assumption was wrong.  Return NULL to model
 * the empty frame table and override the embedded-sprite fallback in
 * pick_texture_for_action(); the green now comes only from the mesh's own
 * lit vertex colours, matching retail's soft glow. */
static void *house_hikari_texture(void)
{
    return NULL;
}

static IDirect3DDevice8 *g_scene1_preload_dev = NULL;

/*
 * Worker-thread entry point — bridges worker_load_set_cb's no-arg
 * signature to the int-returning scene1_preload_house.
 */
static void scene1_preload_house_cb(void)
{
    (void)scene1_preload_house();
}

void scene1_preload_init(struct IDirect3DDevice8 *dev)
{
    g_scene1_preload_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_cb(1 /* INGAME */, scene1_preload_house_cb);
}

int scene1_preload_house(void)
{
    /* E.2 probe — FUN_00474a9a @ 0x474a9a.  Marker for `--align-on-first
     * 0x474a9a`: this is the worker-load INGAME callback that fires once
     * per HOUSE entry on both port and retail. */
    CALL_TRACE_ENTER(0x474a9au);

    /* C8g.1 — engine FUN_0040f64b's 3-table preamble.  Sentinel-resets
     * the per-record tables read by the scene-1 mesh walkers.  Called
     * once at scene-1 entry so the counter-scan in scene1_render_meshes
     * sees a clean sentinel state.  reset_c=1 matches the common engine
     * call pattern (full reset on scene entry).  The full FUN_0040f64b
     * also touches DAT_044e28fc / DAT_0695e07c / DAT_0064e818 and calls
     * FUN_00414902 — deferred until their consumers port. */
    scene1_records_reset(1);

    /* Cf.1 — engine FUN_00436f97 tail (the 200-iter ambient spawn loop
     * at L690-700).  Wiring here is a stand-in: the engine doesn't run
     * FUN_00436f97 on HOUSE entry from title (it's called from sub-
     * scene transitions via FUN_0048526d / FUN_0049e163, both
     * unported).  The gate `ambient_spawn_flag` at stage_palette +
     * 0x1b28 is BSS-zero for HOUSE, so the spawn loop no-ops here
     * unless tests / a future force-flag CLI trip it.  Pose-player
     * + init_stage_defaults run unconditionally to mirror the
     * engine's pre-gate work.  See docs/findings/scene1-postload-
     * init.md for the 25-block survey + Cf.1 scope. */
    scene1_postload_init_stage_defaults();
    scene1_postload_pose_player();
    scene1_postload_ambient_spawn();

    /* Cf — phase-2 walker-array writer (engine FUN_00436f97 alt-stage
     * arm, decomp L34770+).  FUN_00436f97 block 21 runs on new-game HOUSE
     * entry (proven 2026-05-29 via the E.1 call tracer — called once at
     * engine frame 3200, before the first scene1_render_meshes).
     *
     * De-MVP: source the writer's inputs from real engine state instead of
     * the old --force-walker-phase2 0 injection.  load_house_phase2_inputs
     * sets scene_type (0 = HOUSE), ivar8 (engine const 3), stage_positions
     * (the save-record furniture array, seeded from the template exactly as
     * the engine's FUN_0048ffd9 does) and the camera char_mode (record
     * +0x2ce0c = 0).  The camera bias_x/z stand-in (the one remaining MVP
     * value — FUN_00432e50 placement search unported) is applied here too.
     * Both run unconditionally now; HOUSE furniture renders with no flag. */
    scene1_postload_load_house_phase2_inputs();
    scene1_camera_apply_house_groundtruth();
    scene1_postload_walker_phase2_init();

    /* C8j.fin.c — table C smoke wiring.  Fires `_spawn_pickup` and/or
     * `_spawn_world_drop_default` once per HOUSE entry when their CLI
     * overrides are set (default: -1 / -1 → no-op).  Records then tick
     * every INGAME frame via the C8j.3 default-arm wiring.  Pass C/D
     * walker bodies are stubs today — this validates the populator +
     * tick path in production. */
    scene1_postload_smoke_c_spawn();

    /* C8j.fin.b — table B smoke wiring.  Fires `scene1_record_b_spawn_npc`
     * and/or `_entity` once per HOUSE entry with a fake-owner blob seeded
     * with the current spawn pose + identity matrix.  Defaults (-1 / -1)
     * → no-op.  Records sit dormant (FUN_0043ae20 table B tick still
     * stubbed in scene1_sim) — but the allocator + preamble + per-type
     * dispatch is exercised end-to-end. */
    scene1_postload_smoke_b_spawn();

    /* Engine guards: top-of-FUN_00474a9a clamps DAT_0438b4dc to the
     * selector table. We seeded selectors in stage_state at boot, so
     * a no-op here unless future stage-change code wants to refresh. */
    stage_init_house();

    /* Engine FUN_0047281e — 24-byte texture cache clear. Drops all
     * cached texture entries + releases their D3D textures (Win32) so
     * the next mesh_load batch starts fresh. Critical when switching
     * stages so the previous scene's textures don't keep slots from
     * being reused for the new scene's atlas. */
    mesh_tex_cache_reset();

    /* Engine FUN_00474681 — per-stage pre-load.  Probes the engine
     * call site, invokes FUN_0043244c (cache clear, no-op for port).
     * Pointer-set is delegated to stage_palette_init_house below — see
     * stage_palette.h "deviation note" for why. */
    stage_palette_load_for_stage();

    /* FUN_00474681 mesh-load loop (PII.3c) — load the HOUSE stage's
     * `map:` meshes into g_scene_map_meshes[] (engine DAT_068dcca0[]).
     * Ground truth: 11 meshes; index 0 = shop_1st.x (the room), index
     * 1 = shop_jutan.x (the carpet) — draw loop A renders those 2 as
     * the shop interior background.  This must run AFTER
     * mesh_tex_cache_reset() (above) so each mesh_load repopulates the
     * freshly-cleared texture cache that draw loop A's per-slot
     * classify→SetTexture path reads.  See scene_map_meshes.h. */
    scene_map_meshes_reset();
    {
        int map_loaded = scene_map_meshes_load_house(g_scene1_preload_dev);
        fprintf(stderr, "scene1_preload: map meshes loaded=%d "
                "(shop_1st.x room + shop_jutan.x carpet + furniture pool)\n",
                map_loaded);
    }

    /* Stage palette HOUSE record reset + g_stage_palette pointer set.
     * The 2 conditional render-side reads (palette+0x52c, palette+0x108)
     * are zero for HOUSE, so the rest of scene1_preload_house body
     * sees a clean palette pointer. */
    stage_palette_init_house();

    /* Engine FUN_00473c15 — early-returns when *DAT_068dd2f0 == 0,
     * so HOUSE skips it entirely. The 562-line body is DUNGEON-only
     * (enemy/golem/wisp asset loads). Deferred. */

    if (g_stage_palette && g_stage_palette->mode != 0) {
        /* DUNGEON branch — TODO follow-up chip with the corresponding
         * arms of FUN_00474a9a + FUN_00473c15. Today's worker-spawn
         * for state==1 only happens after title fade, so the engine
         * is firmly in HOUSE territory; this guard is for the day
         * stage transitions land. */
        return 0;
    }

    /* chr portrait loop — 21 entries. Each loads
     * "bmp/chr/chr_%02d.bmp" at the per-chr record's width/height.
     * That record array (DAT_0438cec8, stride 0x1416 = 5142 bytes
     * per chr) is BSS-zero at boot, so sprite_load gets w=h=0 today
     * — our sprite_load short-circuits to a 0x0 sprite (no D3D
     * texture created). When chara-record state lands and populates
     * width/height, this loop wakes up automatically. */
    for (int i = 0; i < SCENE1_PRELOAD_CHR_PORTRAIT_COUNT; i++) {
        int32_t chr_id = g_scene1_chr_portrait_ids[i];
        char path[64];
        snprintf(path, sizeof path, "bmp/chr/chr_%02d.bmp", chr_id);
        /* w/h = 0/0 — BSS-zero per-chr record. When chara state
         * lands, these become the real per-portrait dimensions. */
        sprite_load(g_scene1_preload_dev, path, 0, 0,
                    &g_scene1_chr_portraits[i]);
    }

    /* 2 fixed singleton sprite_loads. Engine: FUN_0047193c(3, ...).
     * leve_win.tga is the "ready for adventure" overlay; mood_para
     * is a UI scratch surface. Both are real assets that exist in
     * the vendor BMP/TGA dir.
     *
     * sprite_load returns 1 on success, 0 on failure (see sprite.c). */
    int loads = 0;
    loads += sprite_load(g_scene1_preload_dev, "bmp/leve_win.tga",
                         0x200, 0x100, &g_scene1_leve_win);
    loads += sprite_load(g_scene1_preload_dev, "bmp/mood_para.tga",
                         0x200, 0x200, &g_scene1_mood_para);

    /* Foreground walls/floor/jutan/table loads — engine
     * FUN_0047474e(0) / FUN_004747dc(0) / FUN_0047486a(0) /
     * FUN_004748f8(0) at L73066-73069 of the decompiled body. Each
     * loads the selector-matched asset (1 of the 15 walls / 15 floors
     * / 8 jutans / 16 tables). The remaining variants get pulled in
     * later by the secondary worker bodies B3E/B82/BC6/C0A (those
     * spawners are not called yet — they fire from stage-change
     * paths we haven't surveyed).
     *
     * Returns from _load_foreground_win32 are dispatch counts (0 or 1
     * per loader; sums to ≤4 for HOUSE). */
    loads += scene_walls_load_foreground_win32(g_scene1_preload_dev);
    loads += scene_floor_load_foreground_win32(g_scene1_preload_dev);
    loads += scene_jutan_load_foreground_win32(g_scene1_preload_dev);
    loads += scene_table_load_foreground_win32(g_scene1_preload_dev);

    /* PII.3c — install the stage-texture hooks now that the
     * selector-matched wall/floor/jutan textures are loaded, so draw
     * loop A binds them for the room's kabe/yuka/jutan surfaces. */
    scene1_walker_set_kabe_texture_hook(house_kabe_texture);
    scene1_walker_set_yuka_texture_hook(house_yuka_texture);
    scene1_walker_set_jutan_texture_hook(house_jutan_texture);
    /* Bind the real hikari/window texture (mood_para) so the hikari pass
     * stops falling back to the cyan embedded submesh sprite. */
    scene1_walker_set_animated_texture_hook(house_hikari_texture);

    fprintf(stderr,
            "scene1_preload: HOUSE branch fired — loads=%d "
            "(leve_win + mood_para + walls/floor/jutan/table selectors)\n",
            loads);

    /* C8e.smoke — post-house hook fires AFTER mesh_tex_cache_reset() and
     * the foreground asset loads so any consumer (e.g. --force-pass-d-mesh
     * in main.c) can re-bind cache-dependent state on a clean slate. */
    if (g_scene1_preload_post_house_cb) {
        g_scene1_preload_post_house_cb();
    }

    return loads;
}

#endif /* _WIN32 */
