/*
 * scene_buy.h — buy-phase inventory loader (engine FUN_0047329b @
 *               0x47329b, 151 bytes). Inner body for the AE8 secondary
 *               worker thread (LAB_00452ae8), paired with the
 *               FUN_00452d3e spawner under `g_worker_sec_param == 0`.
 *
 * The body has THREE phases:
 *
 *   1. Dynamic per-item icon loop (page 0).
 *
 *      If both `DAT_06a63bdc` (per-page valid flag) and `DAT_06a63bd4`
 *      (per-page item count) are non-zero, iterate `count` times. Each
 *      iteration reads a 256-byte filename from `&DAT_06a5ead4` (stride
 *      0x100 within the page) and loads it into the sprite array at
 *      `&DAT_073aa7e8` (stride 0x10 = sprite_t).
 *
 *      Engine sprintf format `"bmp/%s"` (`.rdata @ 0x5c864c`).
 *      Engine sprite_load format flag `0x10` (dropped — openrecet
 *      sprite_load doesn't carry format flags). Per-slot dims
 *      `0x200×0x200` regardless of icon kind.
 *
 *      AE8 always reads **page 0** of the per-page state — it does NOT
 *      consult `DAT_0730b56c` (the current-page selector). The B13
 *      sibling body (FUN_0047333b, lands in the next chip) is the
 *      page-indexed variant.
 *
 *      Engine has no bounds check on the loop. The sprite array's
 *      per-page stride is 0xa0 = 10 slots; counts above 10 overflow
 *      into adjacent pages' sprite slots. Faithful in our port: we
 *      provision 10 slots and the Win32 wrapper silently drops
 *      out-of-range writes. Tests can observe the full dispatch
 *      sequence regardless.
 *
 *   2. Fixed `bmp/ivent/chrname.tga` load (`DAT_073cc8d0`, 0x200×0x200).
 *      Always fires, even when the dynamic loop bails on a zero flag.
 *
 *   3. Fixed `bmp/shopmode.tga` load (`DAT_073a9580`, 0x400×0x200).
 *      Always fires.
 *
 * ─── Per-page state ────────────────────────────────────────────────────
 *
 * The engine has 50 per-page blocks (init loop @ FUN_00476... `do {
 * puVar12 += 0xb19c; } while (puVar12 != &DAT_0730fdb4)` — page stride
 * 0x2c670 bytes, end address 0x0730fdb4). AE8 only touches page 0, so
 * this chip only provisions page-0 state. The B13 follow-up chip will
 * promote `g_scene_buy_page0_*` to full per-page arrays.
 *
 *   `g_scene_buy_page0_valid` — engine DAT_06a63bdc (page 0)
 *   `g_scene_buy_page0_count` — engine DAT_06a63bd4 (page 0)
 *   `g_scene_buy_page0_names` — engine DAT_06a5ead4 (page 0)
 *
 * All BSS-zero by default. The buy-phase scene's customer-arrival code
 * (deferred) writes them before kicking the AE8 worker. Until then the
 * body falls through the dynamic loop and only loads the two
 * singletons.
 *
 * ─── Inner-body call shape ─────────────────────────────────────────────
 *
 * Engine LAB_00452ae8 (objdump @ 0x452ae8..0x452b13):
 *
 *     call   0x47329b              ; FUN_0047329b (no args)
 *     push   0x6a49950             ; release loader CS
 *     call   *0x51505c
 *     xor    eax, eax
 *     push   $0x1                  ; (esp now == 1, popped into eax)
 *     mov    [0x6a49950], eax      ; clear loader CS handle
 *     mov    [0x6a4995c], eax      ; clear secondary state flag
 *     mov    [0x6a49960], eax      ; clear secondary state flag
 *     pop    eax                   ; eax = 1
 *     mov    [0x438b1cc], eax      ; per-kind ready=1 state byte
 *     ret
 *
 * No literal arg pushed before the call — AE8's inner-body call is
 * argument-less (unlike B3E/B82/BC6 which pass literal 1). The shared
 * cleanup tail is identical to the other 8 secondary inner bodies and
 * is handled by `worker_load_secondary_thread_proc`'s
 * `worker_load_sec_post_body` step — we just register the body.
 *
 * Worker_load wiring:
 *
 *   `scene_buy_init(dev)` caches the D3D device and registers
 *   `scene_buy_ae8_body` via
 *   `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8, …)`. The body
 *   is dormant until something calls `worker_load_spawn_d3e(0)` (the
 *   buy-phase scene transition will do this — deferred).
 */

#ifndef OPENRECET_SCENE_BUY_H
#define OPENRECET_SCENE_BUY_H

#include <stdint.h>

/* Sprite-array per-page stride is 0xa0 bytes (engine `&DAT_073aa7e8 +
 * page * 0xa0`). 0xa0 / 0x10 (sprite_t) = 10 slots per page. The
 * dynamic loop's effective upper bound is this, even though the engine
 * doesn't check — counts above 10 overflow into adjacent pages. */
#define SCENE_BUY_SLOT_COUNT 10

/* Slot indices for the two fixed singletons. Distinct from the
 * dynamic-loop range so the recording load_fn in tests can disambiguate
 * dispatches by slot index alone. */
#define SCENE_BUY_AE8_SLOT_CHRNAME  10  /* bmp/ivent/chrname.tga */
#define SCENE_BUY_AE8_SLOT_SHOPMODE 11  /* bmp/shopmode.tga      */

/* Per-page state (page 0 only — see banner). */
extern int32_t g_scene_buy_page0_valid;                          /* DAT_06a63bdc */
extern int32_t g_scene_buy_page0_count;                          /* DAT_06a63bd4 */
extern char    g_scene_buy_page0_names[SCENE_BUY_SLOT_COUNT][256]; /* DAT_06a5ead4 */

/* Optional injected loader for tests. Receives the formatted path,
 * slot index, and expected dims. Return value is ignored — tests use
 * it to record dispatches. */
typedef int (*scene_buy_load_fn)(const char *path, int slot,
                                  int w, int h, void *userdata);

/* Pure-C body — engine FUN_0047329b end-to-end. Iterates the dynamic
 * loop up to `g_scene_buy_page0_count` (clamped at SCENE_BUY_SLOT_COUNT
 * for memory safety; engine has no cap), then dispatches the two
 * singletons unconditionally. Returns the total dispatch count.
 * NULL `load_fn` is a counting-only dry run. */
int  scene_buy_ae8_load_with(scene_buy_load_fn load_fn, void *userdata);

/* Engine sprintf format (`bmp/%s`, `.rdata @ 0x5c864c`). Exposed for
 * test anchoring against the .rdata string. */
const char *scene_buy_format_string(void);

/* Reset module state — clears page-0 globals and (on Win32) zeroes the
 * destination sprite_t handles. Tests only. */
void scene_buy_reset(void);

#ifdef _WIN32

#include "sprite.h"

/* Destination sprite slots (engine BSS).
 *   `g_scene_buy_page0_sprites` — engine &DAT_073aa7e8[page 0],
 *     contiguous stride-0x10 array of 10 sprite_t.
 *   `g_scene_buy_chrname`       — engine DAT_073cc8d0 (singleton).
 *   `g_scene_buy_shopmode`      — engine DAT_073a9580 (singleton).
 *
 * The B13 follow-up chip will share the same `&DAT_073aa7e8` array
 * (indexed by the current page) so this stays page-0-only here. */
extern sprite_t g_scene_buy_page0_sprites[SCENE_BUY_SLOT_COUNT];
extern sprite_t g_scene_buy_chrname;
extern sprite_t g_scene_buy_shopmode;

struct IDirect3DDevice8;

/* Cache the D3D device and register the body via
 * worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8, ...). Call once
 * at boot, after the device is created. Idempotent. */
void scene_buy_init(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_BUY_H */
