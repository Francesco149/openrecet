/*
 * scene1_preload.h — scene-1 (INGAME) pre-load entry point.
 *
 * Engine FUN_00474a9a @ 0x474a9a (760 bytes). Triggered from
 * FUN_004547ab's device-recovery dispatch (case 1 INGAME) AND used as
 * the worker_load slot-1 callback so worker_load_spawn() fires the
 * scene-1 asset loads when the title fade resolves into INGAME.
 *
 * This chip ports the *DAT_068dd2f0 == 0 (HOUSE) branch only:
 *
 *   1. Apply stage selector (already in stage_state; explicit hook).
 *   2. Texture cache reset (FUN_0047281e — 24 byte sibling).
 *   3. Set stage palette pointer (FUN_00474681 HOUSE-only path).
 *   4. Skip FUN_00473c15 (it early-returns for HOUSE).
 *   5. chr portrait loop (21 entries, sizes from BSS-zero record table
 *      → dormant until chara records populate; emits sprite_loads at
 *      w=h=0 which our sprite_load no-ops).
 *   6. leve_win + mood_para sprite_loads (2 fixed).
 *   7. 4× foreground `_load_with(0)` for walls/floor/jutan/table
 *      (loads the selector-matched asset of each).
 *
 * The DUNGEON `else` branch — DAT_068dd2f0 != 0 — is dormant for HOUSE
 * (the only stage where we'd land today) and deferred to a follow-up
 * chip alongside FUN_00473c15 (DUNGEON-only) and FUN_00436f97 (710-line
 * sibling that pairs with FUN_00474a9a in the device-recovery dispatch).
 *
 * The worker-side asset loads for the OTHER 14 wall/7 floor/etc. slots
 * happen via the secondary worker bodies (B3E walls, B82 floor, BC6
 * jutan, C0A table — wired since C6 but their secondary spawners
 * (FUN_00452d85/dc1/dfd/e39) aren't called by anyone yet). So today
 * this chip lands one wall/one floor/one jutan/one table on the device,
 * not the full per-stage variant set — but it's the first chip where
 * scene-1 asset loads actually fire from the scene transition.
 */

#ifndef OPENRECET_SCENE1_PRELOAD_H
#define OPENRECET_SCENE1_PRELOAD_H

#include <stdint.h>

/* The 21-entry chr portrait id table from .rdata 0x5c8058. Each entry
 * is an index into the per-chr record array (BSS at DAT_0438cec8,
 * stride 0x1416) whose +0/+4 hold the portrait width/height. The
 * record array is BSS-zero at boot, so all 21 sprite_loads issue with
 * w=h=0 today — they wake up when chara-record state lands. */
#define SCENE1_PRELOAD_CHR_PORTRAIT_COUNT 21
extern const int32_t g_scene1_chr_portrait_ids[SCENE1_PRELOAD_CHR_PORTRAIT_COUNT];

#ifdef _WIN32

struct IDirect3DDevice8;

/*
 * Wire scene1_preload_house as the worker_load slot-1 INGAME callback.
 * Caches `dev` for the foreground sprite_loads. Idempotent.
 *
 * Call once at boot, after scene_walls_init / scene_floor_init /
 * scene_jutan_init / scene_table_init (so their device caches are
 * populated) and after sysassets_init (so the chr portrait slots have
 * somewhere to land). The worker callback runs in the worker thread
 * when scene_post_fade_init → worker_load_spawn fires for INGAME state.
 */
void scene1_preload_init(struct IDirect3DDevice8 *dev);

/*
 * Direct entry point — same as the worker callback but callable from
 * the main thread for tests + manual-trigger flows. Runs the HOUSE
 * branch of FUN_00474a9a; no-op when DAT_068dd2f0 != 0 (DUNGEON
 * branch deferred).
 *
 * Returns the number of "real" sprite_loads issued (counts only the
 * 4 foreground _load_with(0) successes + leve_win + mood_para;
 * chr portraits at w=h=0 don't count).
 */
int scene1_preload_house(void);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_PRELOAD_H */
