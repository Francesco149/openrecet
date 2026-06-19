/*
 * scene_buy.h — buy-phase inventory loaders (engine FUN_0047329b @
 *               0x47329b + FUN_0047333b @ 0x47333b). Inner bodies for
 *               the AE8 + B13 secondary worker threads (LAB_00452ae8 +
 *               LAB_00452b13), paired with the FUN_00452d3e spawner
 *               under `g_worker_sec_param == 0` (AE8) and `!= 0` (B13).
 *
 * AE8 and B13 are siblings:
 *
 *   AE8 (FUN_0047329b, 151 bytes) — page-0 loader + 2 fixed singletons.
 *   B13 (FUN_0047333b, 145 bytes) — current-page-indexed loader, no
 *                                    singletons.
 *
 * They share the same per-page state arrays, so both bodies live in
 * one module. AE8 always reads page 0; B13 reads
 * `g_scene_buy_current_page` (engine `DAT_0730b56c`).
 *
 * ─── AE8 body (FUN_0047329b @ 0x47329b) ────────────────────────────────
 *
 *   Three phases:
 *
 *   1. Dynamic per-item icon loop (page 0). Gated on
 *      `(g_scene_buy_valid[0] != 0 && g_scene_buy_count[0] != 0)`.
 *      Iterates `count` times reading 256-byte names from
 *      `g_scene_buy_names[0]` (engine `&DAT_06a5ead4`) and dispatches
 *      to `g_scene_buy_sprites[0]` (engine `&DAT_073aa7e8`, page 0).
 *      Dims 0x200x0x200. Engine sprintf format `bmp/%s` (.rdata @
 *      0x5c864c). Engine sprite_load format flag 0x10 (dropped).
 *
 *   2. Fixed `bmp/ivent/chrname.tga` → `g_scene_buy_chrname`
 *      (engine `DAT_073cc8d0`), 0x200x0x200. Always fires.
 *
 *   3. Fixed `bmp/shopmode.tga` → `g_scene_buy_shopmode` (engine
 *      `DAT_073a9580`), 0x400x0x200. Always fires.
 *
 * ─── B13 body (FUN_0047333b @ 0x47333b) ────────────────────────────────
 *
 *   Single phase — same shape as AE8 phase 1 but indexed by the
 *   current-page selector instead of page 0, and with no singletons.
 *
 *   Gated on `(g_scene_buy_valid[page] != 0 && g_scene_buy_count[page]
 *   != 0)`. Iterates `count[page]` times reading from
 *   `g_scene_buy_names[page]` and dispatching to
 *   `g_scene_buy_sprites[page]`. Dims 0x200x0x200. Engine sprintf
 *   format `bmp/%s` (.rdata @ 0x5c8680 — different address from AE8's
 *   but same literal). Engine sprite_load format flag 0x11 (dropped).
 *
 *   Range check: engine reads `(&DAT_06a63bdc)[page * 0xb19c]` with
 *   NO bounds check on `page`. Port clamps an out-of-range
 *   `g_scene_buy_current_page` (incl. the -1 sentinel) to a no-op for
 *   memory safety; tests cover this.
 *
 * ─── Per-page state ────────────────────────────────────────────────────
 *
 *   Engine has 50 per-page blocks (init loop @ `do { puVar12 +=
 *   0xb19c; } while (puVar12 != &DAT_0730fdb4)` — page stride 0x2c670
 *   bytes, end 0x0730fdb4). All 50 BSS-zero by default; buy-phase
 *   customer-arrival code (deferred) writes them before kicking AE8
 *   or B13.
 *
 *     `g_scene_buy_current_page` — engine `DAT_0730b56c`. Selector
 *         used by B13; valid range [0, SCENE_BUY_PAGE_COUNT); the
 *         engine also uses -1 as "no page" sentinel. AE8 ignores
 *         this and always reads page 0.
 *     `g_scene_buy_valid[page]`  — engine `&DAT_06a63bdc[page*0xb19c]`
 *     `g_scene_buy_count[page]`  — engine `&DAT_06a63bd4[page*0xb19c]`
 *     `g_scene_buy_names[page][slot][256]` — engine
 *         `&DAT_06a5ead4 + page*0x2c670 + slot*0x100`
 *
 * ─── Slot count + overflow ─────────────────────────────────────────────
 *
 *   Sprite-array per-page stride is 0xa0 bytes (engine `&DAT_073aa7e8
 *   + page * 0xa0`). 0xa0 / 0x10 (sprite_t) = 10 slots per page
 *   (`SCENE_BUY_SLOT_COUNT`). Engine has no per-slot bounds check;
 *   counts above 10 overflow into adjacent pages' sprite slots. Port
 *   clamps the loop at SCENE_BUY_SLOT_COUNT for memory safety; tests
 *   cover the clamp behaviour.
 *
 * ─── Inner-body call shape ─────────────────────────────────────────────
 *
 *   Both LAB_00452ae8 and LAB_00452b13 just `call <body>` with NO
 *   pre-arg push (argument-less). The shared cleanup tail is handled
 *   by `worker_load_secondary_thread_proc`'s post-body step — we just
 *   register the bodies.
 *
 * Worker_load wiring:
 *
 *   `scene_buy_init(dev)` caches the D3D device and registers BOTH
 *   bodies via `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8,
 *   …)` and `(…SEC_BODY_B13, …)`. Both dormant until something calls
 *   `worker_load_spawn_d3e(0)` (AE8) or `_d3e(non-zero)` (B13) — the
 *   buy-phase scene transition will do this once it ports.
 */

#ifndef OPENRECET_SCENE_BUY_H
#define OPENRECET_SCENE_BUY_H

#include <stdint.h>
#include <stddef.h>   /* size_t (scene_buy_parse_stage_buffer) */

/* Per-page block count. Engine init loop end-address: (0x0730fdb4 -
 * 0x06a63bd4) / 0x2c670 = 50. */
#define SCENE_BUY_PAGE_COUNT 50

/* Sprite-array per-page stride is 0xa0 bytes (engine `&DAT_073aa7e8 +
 * page * 0xa0`). 0xa0 / 0x10 (sprite_t) = 10 slots per page. */
#define SCENE_BUY_SLOT_COUNT 10

/* Slot indices for the two AE8-only fixed singletons. Distinct from
 * the dynamic-loop range so the recording load_fn in tests can
 * disambiguate dispatches by slot index alone. */
#define SCENE_BUY_AE8_SLOT_CHRNAME  10  /* bmp/ivent/chrname.tga */
#define SCENE_BUY_AE8_SLOT_SHOPMODE 11  /* bmp/shopmode.tga      */

/* Current-page selector — engine `DAT_0730b56c`. Read by B13. Range
 * [0, SCENE_BUY_PAGE_COUNT); -1 is the engine's "no page" sentinel.
 * BSS-zero default (i.e. defaults to page 0; the buy-phase entry path
 * writes this before kicking B13). */
extern int32_t g_scene_buy_current_page;

/* Per-page state arrays (50 pages × …). */
extern int32_t g_scene_buy_valid[SCENE_BUY_PAGE_COUNT];                              /* DAT_06a63bdc */
extern int32_t g_scene_buy_count[SCENE_BUY_PAGE_COUNT];                              /* DAT_06a63bd4 */
extern char    g_scene_buy_names[SCENE_BUY_PAGE_COUNT][SCENE_BUY_SLOT_COUNT][256];   /* DAT_06a5ead4 */

/* Optional injected loader for tests. Receives the formatted path,
 * slot index, and expected dims. Return value is ignored — tests use
 * it to record dispatches. */
typedef int (*scene_buy_load_fn)(const char *path, int slot,
                                  int w, int h, void *userdata);

/* AE8 body — engine FUN_0047329b end-to-end. Reads page 0
 * unconditionally. Returns total dispatch count (dynamic + 2 fixed
 * singletons). NULL `load_fn` is a counting-only dry run. */
int  scene_buy_ae8_load_with(scene_buy_load_fn load_fn, void *userdata);

/* B13 body — engine FUN_0047333b end-to-end. Reads page
 * `g_scene_buy_current_page`. Out-of-range page (incl. -1) is a no-op
 * (returns 0). Returns total dispatch count (just the dynamic loop —
 * no singletons). NULL `load_fn` is a counting-only dry run. */
int  scene_buy_b13_load_with(scene_buy_load_fn load_fn, void *userdata);

/* Engine sprintf format (`bmp/%s`). Same literal at engine .rdata
 * 0x5c864c (AE8) and 0x5c8680 (B13); we expose one getter. */
const char *scene_buy_format_string(void);

/* Per-stage character-sprite NAME parser — engine FUN_00475270 block #4
 * (the `grpNN:` arm).  Pure parse of ONE customer's `file:` data buffer into
 * g_scene_buy_names[rec][*] + g_scene_buy_count[rec] (the standee filenames the
 * AE8/B13 workers load).  The storage-backed driver lives in tables.c (called
 * from tables_load_all after the kyaku + chara tables); kept pure here so the
 * host suite can test it without the storage link dep. */
void scene_buy_parse_stage_buffer(int rec, const char *buf, size_t len);

/* Reset module state — clears all per-page globals and (on Win32)
 * zeroes the destination sprite_t handles. Tests only. */
void scene_buy_reset(void);

#ifdef _WIN32

#include "sprite.h"

/* Destination sprite slots (engine BSS).
 *   `g_scene_buy_sprites[page][slot]` — engine `&DAT_073aa7e8`,
 *      laid out as a 50×10 grid of sprite_t (50 pages × 10 slots,
 *      stride 0xa0 between pages, 0x10 between slots).
 *   `g_scene_buy_chrname`  — engine `DAT_073cc8d0` (AE8-only singleton).
 *   `g_scene_buy_shopmode` — engine `DAT_073a9580` (AE8-only singleton).
 */
extern sprite_t g_scene_buy_sprites[SCENE_BUY_PAGE_COUNT][SCENE_BUY_SLOT_COUNT];
extern sprite_t g_scene_buy_chrname;
extern sprite_t g_scene_buy_shopmode;

struct IDirect3DDevice8;

/* Cache the D3D device and register BOTH bodies via
 * worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8, ...) and
 * (...SEC_BODY_B13, ...). Call once at boot, after the device is
 * created. Idempotent. */
void scene_buy_init(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_BUY_H */
