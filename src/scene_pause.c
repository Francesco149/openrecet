/*
 * scene_pause.c — see scene_pause.h.
 *
 * Engine sources:
 *   - Unnamed FPU init @ 0x435873 (86 bytes; Ghidra missed it)
 *   - FUN_00473a3e         @ 0x473a3e (453 bytes)
 *
 * Both fire from the C4E secondary worker thread proc (LAB_00452c4e at
 * 0x452c4e), in that order, before the shared cleanup tail.
 */

#include "scene_pause.h"

#include "worker_load.h"

/* ─── module state ───────────────────────────────────────────────────── */

int32_t g_scene_pause_selector = 0;

int32_t g_scene_pause_state_b150 = 0;
int32_t g_scene_pause_state_b158 = 0;
int32_t g_scene_pause_state_b15c = 0;
int32_t g_scene_pause_state_ac18 = 0;
int32_t g_scene_pause_state_ac1c = 0;
int32_t g_scene_pause_state_ac20 = 0;
float   g_scene_pause_state_abf4 = 0.0f;
float   g_scene_pause_state_abf8 = 0.0f;
float   g_scene_pause_state_ac00 = 0.0f;
float   g_scene_pause_state_ac04 = 0.0f;

/* ─── FPU init (engine unnamed @ 0x435873) ───────────────────────────── */

void scene_pause_state_init(void)
{
    /* Write order mirrors the engine asm (see scene_pause.h banner for
     * the disassembly). Polarity matters only insofar as a future
     * consumer can rely on these being the exact post-init values; the
     * end-state is the same regardless of order. */
    g_scene_pause_state_b150 = 1;     /* DAT_0438b150 = 1   */
    g_scene_pause_state_ac00 = 32.0f; /* DAT_0438ac00 = 32  */
    g_scene_pause_state_ac20 = 0;     /* DAT_0438ac20 = 0   */
    g_scene_pause_state_ac18 = 0;     /* DAT_0438ac18 = 0   */
    g_scene_pause_state_ac04 = 80.0f; /* DAT_0438ac04 = 80  */
    g_scene_pause_state_b158 = 0;     /* DAT_0438b158 = 0   */
    g_scene_pause_state_b15c = 0;     /* DAT_0438b15c = 0   */
    g_scene_pause_state_abf4 = 32.0f; /* DAT_0438abf4 = 32  */
    g_scene_pause_state_ac1c = 0;     /* DAT_0438ac1c = 0   */
    g_scene_pause_state_abf8 = 80.0f; /* DAT_0438abf8 = 80  */
}

/* ─── asset-load helpers ─────────────────────────────────────────────── */

/* Per-slot static metadata: fixed filename (NULL = use the selector) +
 * expected dims. Order matches the engine's call sequence in
 * FUN_00473a3e. */
static const struct {
    const char *fname;   /* NULL for slot 0 (selector-driven) */
    int         w, h;
} g_scene_pause_assets[SCENE_PAUSE_LOAD_COUNT] = {
    /* 0  pause / pause_endless (selector-driven)            */ { 0,                       0x400, 0x200 },
    /* 1  pause_bg_rete                                      */ { "bmp/pause_bg_rete.tga", 0x400, 0x200 },
    /* 2  result_bord01                                      */ { "bmp/result_bord01.tga", 0x200, 0x100 },
    /* 3  dungeonbord                                        */ { "bmp/dungeonbord.tga",   0x400, 0x200 },
    /* 4..11  sousa portraits (cursor headshots)             */
    { "bmp/sousa_lui.tga",     0x400, 0x200 },
    { "bmp/sousa_sya.tga",     0x400, 0x200 },
    { "bmp/sousa_cai.tga",     0x400, 0x200 },
    { "bmp/sousa_tel.tga",     0x400, 0x200 },
    { "bmp/sousa_era.tga",     0x400, 0x200 },
    { "bmp/sousa_nag.tga",     0x400, 0x200 },
    { "bmp/sousa_grf.tga",     0x400, 0x200 },
    { "bmp/sousa_arm.tga",     0x400, 0x200 },
    /* 12..19 status portraits (full character body)         */
    { "bmp/st_ryui.tga",       0x200, 0x200 },
    { "bmp/st_sya.tga",        0x200, 0x200 },
    { "bmp/st_caillou.tga",    0x200, 0x200 },
    { "bmp/st_tiers.tga",      0x200, 0x200 },
    { "bmp/st_eran.tga",       0x200, 0x200 },
    { "bmp/st_nagi.tga",       0x200, 0x200 },
    { "bmp/st_griffe.tga",     0x200, 0x200 },
    { "bmp/st_aruma.tga",      0x200, 0x200 },
};

/* Engine .rdata strings:
 *     s_bmp_pause_endless_tga_005c8b6c — "bmp/pause_endless.tga"
 *     s_bmp_pause_tga_005c8b84         — "bmp/pause.tga"
 */
static const char *const g_scene_pause_endless_path = "bmp/pause_endless.tga";
static const char *const g_scene_pause_normal_path  = "bmp/pause.tga";

static const char *scene_pause_slot0_filename(void)
{
    /* Engine polarity at FUN_00473a3e prologue: a == 2 || a == 3 →
     * endless, else normal. */
    const int32_t sel = g_scene_pause_selector;
    return (sel == 2 || sel == 3) ? g_scene_pause_endless_path
                                   : g_scene_pause_normal_path;
}

const char *scene_pause_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_PAUSE_LOAD_COUNT) return 0;
    if (slot == 0) return scene_pause_slot0_filename();
    return g_scene_pause_assets[slot].fname;
}

int scene_pause_slot_dims(int slot, int *out_w, int *out_h)
{
    if (slot < 0 || slot >= SCENE_PAUSE_LOAD_COUNT) return 0;
    if (out_w) *out_w = g_scene_pause_assets[slot].w;
    if (out_h) *out_h = g_scene_pause_assets[slot].h;
    return 1;
}

/* ─── pure-C body ────────────────────────────────────────────────────── */

int scene_pause_load_with(scene_pause_load_fn load_fn, void *userdata)
{
    /* Engine FUN_00473a3e is a straight-line 20-call sequence — no
     * loop bounds, no per-slot predicate. We iterate in slot order so
     * tests can observe deterministic dispatch ordering matching the
     * engine. */
    int loads = 0;
    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        const char *fname = (i == 0) ? scene_pause_slot0_filename()
                                     : g_scene_pause_assets[i].fname;
        if (load_fn) load_fn(fname, i,
                             g_scene_pause_assets[i].w,
                             g_scene_pause_assets[i].h,
                             userdata);
        loads++;
    }
    return loads;
}

/* ─── reset ──────────────────────────────────────────────────────────── */

static void scene_pause_state_clear(void)
{
    g_scene_pause_selector   = 0;
    g_scene_pause_state_b150 = 0;
    g_scene_pause_state_b158 = 0;
    g_scene_pause_state_b15c = 0;
    g_scene_pause_state_ac18 = 0;
    g_scene_pause_state_ac1c = 0;
    g_scene_pause_state_ac20 = 0;
    g_scene_pause_state_abf4 = 0.0f;
    g_scene_pause_state_abf8 = 0.0f;
    g_scene_pause_state_ac00 = 0.0f;
    g_scene_pause_state_ac04 = 0.0f;
}

/* ─── Win32 worker_load wiring + sprite storage ─────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

sprite_t g_scene_pause_pause;
sprite_t g_scene_pause_bg_rete;
sprite_t g_scene_pause_result_bord01;
sprite_t g_scene_pause_dungeonbord;
sprite_t g_scene_pause_sousa[SCENE_PAUSE_SOUSA_COUNT];
sprite_t g_scene_pause_status[SCENE_PAUSE_STATUS_COUNT];

static IDirect3DDevice8 *g_scene_pause_dev = 0;

/* Slot → destination sprite_t. Mirrors the engine's BSS layout. */
static sprite_t *scene_pause_slot_dest(int slot)
{
    switch (slot) {
        case 0:  return &g_scene_pause_pause;
        case 1:  return &g_scene_pause_bg_rete;
        case 2:  return &g_scene_pause_result_bord01;
        case 3:  return &g_scene_pause_dungeonbord;
        default: break;
    }
    if (slot >= 4 && slot <= 11) return &g_scene_pause_sousa[slot - 4];
    if (slot >= 12 && slot <= 19) return &g_scene_pause_status[slot - 12];
    return 0;
}

static int win32_load_fn(const char *path, int slot, int w, int h,
                          void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    sprite_t *dst = scene_pause_slot_dest(slot);
    if (!dst) return 0;
    /* Engine: FUN_0047193c(0xc, dst, name, w, h). Format flag 0xc is
     * dropped — same as all other sprite_load call sites. */
    return sprite_load(dev, path, (uint32_t)w, (uint32_t)h, dst);
}

static void scene_pause_body(void)
{
    /* Engine LAB_00452c4e (objdump @ 0x452c4e..c53):
     *
     *     call 0x435873   ; pause-state FPU init
     *     call 0x473a3e   ; 20-asset load
     *
     * Order matters: the FPU init writes the (32,80) pause-layout
     * constants that any consumer of pause-menu rendering reads. Both
     * land in the same secondary worker tick. */
    scene_pause_state_init();
    scene_pause_load_with(win32_load_fn, g_scene_pause_dev);
}

void scene_pause_init(struct IDirect3DDevice8 *dev)
{
    g_scene_pause_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C4E, scene_pause_body);
}

void scene_pause_reset(void)
{
    /* Win32: zero the sprite_t handles (matches the wall/floor/jutan
     * pattern; lost-device handling is deferred). */
    g_scene_pause_pause.tex = 0;
    g_scene_pause_pause.width = 0;
    g_scene_pause_pause.height = 0;
    g_scene_pause_bg_rete.tex = 0;
    g_scene_pause_bg_rete.width = 0;
    g_scene_pause_bg_rete.height = 0;
    g_scene_pause_result_bord01.tex = 0;
    g_scene_pause_result_bord01.width = 0;
    g_scene_pause_result_bord01.height = 0;
    g_scene_pause_dungeonbord.tex = 0;
    g_scene_pause_dungeonbord.width = 0;
    g_scene_pause_dungeonbord.height = 0;
    for (int i = 0; i < SCENE_PAUSE_SOUSA_COUNT; i++) {
        g_scene_pause_sousa[i].tex = 0;
        g_scene_pause_sousa[i].width = 0;
        g_scene_pause_sousa[i].height = 0;
    }
    for (int i = 0; i < SCENE_PAUSE_STATUS_COUNT; i++) {
        g_scene_pause_status[i].tex = 0;
        g_scene_pause_status[i].width = 0;
        g_scene_pause_status[i].height = 0;
    }
    scene_pause_state_clear();
}

#else  /* !_WIN32 — Linux test build */

void scene_pause_reset(void)
{
    scene_pause_state_clear();
}

#endif /* _WIN32 */
