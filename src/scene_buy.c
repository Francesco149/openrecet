/*
 * scene_buy.c — see scene_buy.h.
 *
 * Engine sources:
 *   - FUN_0047329b @ 0x47329b (151 bytes) — AE8 body
 *   - FUN_0047333b @ 0x47333b (145 bytes) — B13 body
 */

#include "scene_buy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "worker_load.h"

/* ─── module state ────────────────────────────────────────────────────── */

int32_t g_scene_buy_current_page = 0;
int32_t g_scene_buy_valid[SCENE_BUY_PAGE_COUNT];
int32_t g_scene_buy_count[SCENE_BUY_PAGE_COUNT];
char    g_scene_buy_names[SCENE_BUY_PAGE_COUNT][SCENE_BUY_SLOT_COUNT][256];

/* Engine s_bmp__s_005c864c (AE8) and s_bmp__s_005c8680 (B13) — same
 * "bmp/%s" literal at two .rdata addresses. */
static const char *const g_scene_buy_path_fmt = "bmp/%s";

const char *scene_buy_format_string(void)
{
    return g_scene_buy_path_fmt;
}

/* ── per-stage character-sprite NAME parser (FUN_00475270 block #4 grp arm) ──
 * all.c:74568-74716.  Pure parse of ONE customer's `file:` data buffer into the
 * per-page standee-NAME table the AE8/B13 workers load.  The startup driver
 * (tables.c: stage_files) reads each customer's file via storage and calls this;
 * keeping the parse pure (no storage) lets the host suite test it + keeps
 * scene_buy.o free of the storage link dep.
 *
 *   `grpNN:<path>` → g_scene_buy_names[rec][NN] = <path>, g_scene_buy_count[rec]++
 *
 * (NN two digits, ':' at +5, path at +6 — exactly the engine's local_27c+0x26.)
 * Poses are 512² standees under bmp/ (e.g. ivent/01recette_NN.tga); the actual
 * filenames come from the user's data files.  Without this the names table is
 * empty ⇒ both loaders see count==0 ⇒ no character art (text-only scene).
 *
 * PORT-DEBT(cs-stage-msg): the `seNN:` + `msg%02d:` arms (the customer's normal
 * per-line dialogue + its per-line grp/se index) are NOT parsed — the tutorial
 * uses the SCRIPTED machine's dialogue and its CHR ops drive b54c/b550 directly.
 *
 * NOTE the engine name table is 20 slots/record but the sprite array is 10/page
 * (flat); a record with >10 grp slots bleeds into the next page engine-side.  The
 * port clamps storage to SCENE_BUY_SLOT_COUNT (the dense low slots the tutorial
 * uses); count still tracks every grp line so the loader's clamp matches. */
void scene_buy_parse_stage_buffer(int rec, const char *buf, size_t len)
{
    if (rec < 0 || rec >= SCENE_BUY_PAGE_COUNT || !buf) return;
    const char *end = buf + len;
    const char *p = buf;
    while (p < end && *p) {
        char line[512];
        int n = 0;
        while (p < end && *p && *p != '\r' && *p != '\n' && n < (int)sizeof(line) - 1)
            line[n++] = *p++;
        line[n] = '\0';
        while (p < end && (*p == '\r' || *p == '\n')) p++;   /* eat terminator(s) */
        if (line[0] == '\0' || line[0] == '/') continue;     /* blank / comment */

        if (line[0] == 'g' && line[1] == 'r' && line[2] == 'p') {
            int nn = atoi(line + 3);
            if (nn >= 0 && nn < SCENE_BUY_SLOT_COUNT) {
                const char *fn = line + 6;
                int k = 0;
                while (fn[k] != '\0' && k < 255) {
                    g_scene_buy_names[rec][nn][k] = fn[k];
                    k++;
                }
                g_scene_buy_names[rec][nn][k] = '\0';
            }
            g_scene_buy_count[rec] += 1;             /* +0x5144: every grp line */
        }
    }
}

/* ─── shared page-loop helper ────────────────────────────────────────── */

/* Dispatches the per-page dynamic loop common to both AE8 (page=0) and
 * B13 (page=current). Returns the number of dispatches made; 0 if the
 * page is out of range, the valid flag is 0, or the count is 0. */
static int scene_buy_page_dispatch(int page,
                                    scene_buy_load_fn load_fn,
                                    void *userdata)
{
    if (page < 0 || page >= SCENE_BUY_PAGE_COUNT) return 0;
    if (g_scene_buy_valid[page] == 0)             return 0;
    if (g_scene_buy_count[page] == 0)             return 0;

    int n = g_scene_buy_count[page];
    /* Engine has no cap. Per-page sprite array stride is 0xa0 = 10
     * slots; counts above 10 overflow into adjacent pages' sprite
     * memory engine-side. Port clamps for memory safety. */
    if (n > SCENE_BUY_SLOT_COUNT) n = SCENE_BUY_SLOT_COUNT;

    int loads = 0;
    for (int i = 0; i < n; i++) {
        char path[256];
        snprintf(path, sizeof(path), g_scene_buy_path_fmt,
                 g_scene_buy_names[page][i]);
        if (load_fn) load_fn(path, i, 0x200, 0x200, userdata);
        loads++;
    }
    return loads;
}

/* ─── AE8 body (FUN_0047329b @ 0x47329b) ────────────────────────────── */

int scene_buy_ae8_load_with(scene_buy_load_fn load_fn, void *userdata)
{
    /* Phase 1 — dynamic per-item icon loop, page 0. Engine reads
     * `&DAT_06a63bdc` (no index) and `&DAT_06a63bd4` (no index) —
     * always page 0, independent of DAT_0730b56c. */
    int loads = scene_buy_page_dispatch(0, load_fn, userdata);

    /* Phase 2 — fixed chrname.tga (always fires).
     * Engine: FUN_0047193c(0x10, &DAT_073cc8d0,
     *                       s_bmp_ivent_chrname_tga_005c8654,
     *                       0x200, 0x200). */
    if (load_fn) load_fn("bmp/ivent/chrname.tga",
                          SCENE_BUY_AE8_SLOT_CHRNAME,
                          0x200, 0x200, userdata);
    loads++;

    /* Phase 3 — fixed shopmode.tga (always fires).
     * Engine: FUN_0047193c(0x10, &DAT_073a9580,
     *                       s_bmp_shopmode_tga_005c866c,
     *                       0x400, 0x200). */
    if (load_fn) load_fn("bmp/shopmode.tga",
                          SCENE_BUY_AE8_SLOT_SHOPMODE,
                          0x400, 0x200, userdata);
    loads++;

    return loads;
}

/* ─── B13 body (FUN_0047333b @ 0x47333b) ────────────────────────────── */

int scene_buy_b13_load_with(scene_buy_load_fn load_fn, void *userdata)
{
    /* Single phase — same as AE8 phase 1 but page-indexed by the
     * current-page selector. Engine: `(&DAT_06a63bdc)[DAT_0730b56c *
     * 0xb19c]` etc. Out-of-range page (incl. -1) is a no-op — engine
     * would OOB but our port bails cleanly via the helper's range
     * check. */
    return scene_buy_page_dispatch(g_scene_buy_current_page,
                                    load_fn, userdata);
}

/* ─── reset ──────────────────────────────────────────────────────────── */

static void scene_buy_state_clear(void)
{
    g_scene_buy_current_page = 0;
    memset(g_scene_buy_valid, 0, sizeof(g_scene_buy_valid));
    memset(g_scene_buy_count, 0, sizeof(g_scene_buy_count));
    memset(g_scene_buy_names, 0, sizeof(g_scene_buy_names));
}

/* ─── Win32 worker_load wiring + sprite storage ─────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

sprite_t g_scene_buy_sprites[SCENE_BUY_PAGE_COUNT][SCENE_BUY_SLOT_COUNT];
sprite_t g_scene_buy_chrname;
sprite_t g_scene_buy_shopmode;

static IDirect3DDevice8 *g_scene_buy_dev = 0;

/* AE8 Win32 wrapper: per-page-0 sprites for the dynamic range, plus
 * the two AE8-only singletons. */
static int win32_load_fn_ae8(const char *path, int slot, int w, int h,
                              void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    sprite_t *dst;
    if (slot >= 0 && slot < SCENE_BUY_SLOT_COUNT) {
        dst = &g_scene_buy_sprites[0][slot];
    } else if (slot == SCENE_BUY_AE8_SLOT_CHRNAME) {
        dst = &g_scene_buy_chrname;
    } else if (slot == SCENE_BUY_AE8_SLOT_SHOPMODE) {
        dst = &g_scene_buy_shopmode;
    } else {
        return 0;
    }
    return sprite_load(dev, path, (uint32_t)w, (uint32_t)h, dst);
}

/* B13 Win32 wrapper: per-page sprites only, indexed by the current
 * page (already range-checked by scene_buy_page_dispatch before we
 * get here, so this is defensive). */
static int win32_load_fn_b13(const char *path, int slot, int w, int h,
                              void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    int page = g_scene_buy_current_page;
    if (page < 0 || page >= SCENE_BUY_PAGE_COUNT)  return 0;
    if (slot < 0 || slot >= SCENE_BUY_SLOT_COUNT)  return 0;
    return sprite_load(dev, path, (uint32_t)w, (uint32_t)h,
                        &g_scene_buy_sprites[page][slot]);
}

static void scene_buy_ae8_body(void)
{
    scene_buy_ae8_load_with(win32_load_fn_ae8, g_scene_buy_dev);
}

static void scene_buy_b13_body(void)
{
    scene_buy_b13_load_with(win32_load_fn_b13, g_scene_buy_dev);
}

void scene_buy_init(struct IDirect3DDevice8 *dev)
{
    g_scene_buy_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8, scene_buy_ae8_body);
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B13, scene_buy_b13_body);
}

void scene_buy_reset(void)
{
    for (int p = 0; p < SCENE_BUY_PAGE_COUNT; p++) {
        for (int s = 0; s < SCENE_BUY_SLOT_COUNT; s++) {
            g_scene_buy_sprites[p][s].tex    = 0;
            g_scene_buy_sprites[p][s].width  = 0;
            g_scene_buy_sprites[p][s].height = 0;
        }
    }
    g_scene_buy_chrname.tex     = 0;
    g_scene_buy_chrname.width   = 0;
    g_scene_buy_chrname.height  = 0;
    g_scene_buy_shopmode.tex    = 0;
    g_scene_buy_shopmode.width  = 0;
    g_scene_buy_shopmode.height = 0;
    scene_buy_state_clear();
}

#else  /* !_WIN32 — Linux test build */

void scene_buy_reset(void)
{
    scene_buy_state_clear();
}

#endif /* _WIN32 */
