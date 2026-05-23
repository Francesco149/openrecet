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

#include <stdint.h>

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
 *   DAT_056daf88           → g_scene1_player_ground_y    (floor-Y the
 *                            player is standing on; differs from player_pos.y
 *                            which is the animated render height — engine
 *                            snaps player_pos.y = ground_y on landing.
 *                            Used by C8h.4b types 0x41 / 0x62.)
 *   DAT_056daff4           → g_scene1_scene_counter      (per-scene int
 *                            tick counter, reset at scene entry.  Type 0x62
 *                            kill-gates on `counter <= 0x2c`.)
 *   DAT_056db05c           → g_scene1_camera_yaw_alt     (second camera
 *                            yaw, read by type 0x4a's sin/cos pos calc.
 *                            Distinct from _DAT_073de39c above — they have
 *                            different writers; sharing is unsafe.)
 */
extern float g_scene1_player_pos[3];
extern float g_scene1_spawn_origin[3];
extern int   g_scene1_scene_alive;
extern float g_scene1_camera_yaw;
extern float g_scene1_camera_anchor[2];
extern float g_scene1_player_ground_y;
extern int   g_scene1_scene_counter;
extern float g_scene1_camera_yaw_alt;

/*
 * NPC tables consumed by the C8h.4c anchor-snap handlers.
 *
 * Per the docs/findings/scene1-people-table.md survey, the engine's
 * "people" table at DAT_0076bd54 is 128 entries × 2980 B each (a full
 * NPC record with AI/sprite/dialog state).  Integrator handlers
 * 0x1a / 0x78 / 0x75 / 0x93 read only ~10 header fields out of that
 * 2980 B; modeling those header fields explicitly here keeps BSS to
 * 4.5 KB (vs. 372 KB for layout-verbatim).  Future ports that need
 * the AI/sprite state should expand the struct or move to a flat
 * uint8_t buffer.
 *
 *   field name      engine byte off  meaning
 *   pos[0..2]       +0x00 / 04 / 08  primary world-space position
 *   target[0..2]    +0x0c / 10 / 14  secondary pos (anchor target)
 *   alive           +0x44            0 = empty slot; 1/2 alive states
 *   action          +0x5c            state-machine ID
 *   state_counter   +0x910           0/1/2 distance-state from FUN_00430c6d
 *   cooldown        +0x934           decrementing per-frame counter
 */
typedef struct {
    float   pos[3];
    float   target[3];
    int32_t alive;
    int32_t action;
    int32_t state_counter;
    int32_t cooldown;
} scene1_people_entry_t;

#define SCENE1_PEOPLE_COUNT 128                /* engine cap confirmed in C8h.4a */
extern scene1_people_entry_t g_scene1_people[SCENE1_PEOPLE_COUNT];

/*
 * Stride-0xf8 NPC table at DAT_056db120 — integrator type 0x4a reads
 * pos.xyz + yaw.  Cap is unconfirmed (no clear loop bound found); 256
 * is a generous safety margin.  Bounds-checked on read.
 *
 *   pos[0..2]   +0x00 / 04 / 08  (engine: DAT_056db120 / 124 / 128)
 *   yaw         +0x24            (engine: DAT_056db144)
 */
typedef struct {
    float pos[3];
    float yaw;
} scene1_npc_f8_entry_t;

#define SCENE1_NPC_F8_COUNT 256                /* best-guess generous cap */
extern scene1_npc_f8_entry_t g_scene1_npc_table_f8[SCENE1_NPC_F8_COUNT];

/*
 * Activation-gate table at DAT_0695f1e0 — integrator types 0x12 / 0x13
 * / 0x14 gate on this entry's first field.  The engine table has stride
 * 0xa8 dw (672 B) per entry holding much more state, but the integrator
 * only reads the gate field; flatten to one int per entry.  Cap = 512
 * matches FUN_0043a5d9's iteration.  Bounds-checked.
 */
#define SCENE1_NPC_ACTIVATION_COUNT 512
extern int32_t g_scene1_npc_activation[SCENE1_NPC_ACTIVATION_COUNT];

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
