/*
 * scene1_particles_tick.h — per-tick integrator for the scene-1
 * particle table (table A, 4096 × 0x25 dw slots, sentinel TYPE == -1).
 *
 * Chip C8h.1 (2026-05-23).  Ports the outer dispatch shape of engine
 * FUN_0040fb3a @ 0x40fb3a (8071 B, ~95 distinct particle types) plus
 * 4 of the ~95 type handlers:
 *
 *   types 6, 7, 8, 9 — camera-orbit attract
 *   type 0x20        — player-snap; every 4 ticks chains spawn 0x21
 *   type 0x21        — cone-spread velocity sampling (gated by table B)
 *
 * The other ~91 handlers land via C8h.2/.3/.4 — see
 * `docs/findings/scene1-particles-tick.md` for the chip ladder.
 *
 * Engine call sites of FUN_0040fb3a (none wired today):
 *
 *   FUN_004427d3 (30 B)    — 6-call thin wrapper: open + tick + close
 *   FUN_004536cb (1745 B)  — sim-side counterpart of FUN_004547ab
 *                            case-1; the per-tick INGAME caller
 *   FUN_00442cef (2490 B)  — pre-render gate + save-game writer
 *   FUN_00436f97 (4788 B)  — scene-entry reset + 200-iter spawn loop
 *   FUN_0048dbfb (2209 B)  — scene-transition flush (forces count=max)
 *
 * scene1_particles_tick() runs the integrator over ALL 4096 slots
 * regardless of g_scene1_records_a_count — the engine's loop is
 * unconditional `do { ... } while (i != 0x1000);`.  The active-count
 * is for render walkers, not for sim ticks.
 *
 * Per-frame open `FUN_00414929` (1465 B — ticks two non-particle
 * entity tables at DAT_00730c30 and DAT_0064e8a0) is a SEPARATE
 * concern handled by a later chip.  Provisional no-op for C8h.1.
 */
#ifndef SCENE1_PARTICLES_TICK_H
#define SCENE1_PARTICLES_TICK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Engine globals consumed by the integrator.  Exposed as writable
 * extern stubs so downstream ports + tests can populate them.  All
 * are BSS-zero today; the type-6..9 / 0x20 handlers read them every
 * tick they fire, and either set up positions (anchor handlers) or
 * pin the position to the player (snap handlers).
 *
 *   DAT_056da1d8/dc/e0     → g_scene1_player_pos[3]      (player x,y,z)
 *   DAT_056da1f0/f4/f8     → g_scene1_spawn_origin[3]    (override anchor)
 *   DAT_056dae8c           → g_scene1_scene_alive        (kill gate; 0 → kill)
 *   _DAT_073de39c          → g_scene1_camera_yaw         (radians)
 *   _DAT_073de328/30       → g_scene1_camera_anchor[2]   (x, z only)
 */
extern float g_scene1_player_pos[3];
extern float g_scene1_spawn_origin[3];
extern int   g_scene1_scene_alive;
extern float g_scene1_camera_yaw;
extern float g_scene1_camera_anchor[2];

/*
 * Tick the integrator across all 4096 slots of table A.  Mirrors
 * engine FUN_0040fb3a one-shot semantics: callers run it per sim
 * frame from FUN_004536cb's INGAME branch (unported).
 *
 * The leading per-frame open FUN_00414929 is a no-op in C8h.1.
 */
void scene1_particles_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_PARTICLES_TICK_H */
