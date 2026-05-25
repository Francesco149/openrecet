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
 *   sister_720      +0x720           "interactable" gate (== 0 → spawnable)
 *   sister_724      +0x724           "interactable" gate (== 0 → spawnable)
 *
 * sister_720 / sister_724 were added in C8j.9a to back FUN_0044376a's
 * 0x68 spawn body, which iterates the people table for an entry that is
 * alive==1 AND both sister gates are zero AND within 16.0 horizontal
 * units of the spawn owner.  Engine accesses them via base
 * `DAT_0076c478` (= entry 0 byte +0x724) with `piVar13[-1]` for +0x720,
 * `*piVar13` for +0x724.
 *
 * C8jb.2 (combat SM Phase B head) additions:
 *
 *   combat_cooldown_5  +0x710  per-NPC active-cooldown countdown; gates
 *                              the attacker scan in FUN_0043865e
 *                              (`local_30[-5] > 0` → SM skips this NPC).
 *                              When 0, the NPC is targetable.
 *   alive_alias_24     +0x7b4  secondary aliveness flag.  Combat SM gates
 *                              `alive == 1 || (alive == 2 && alias_24 != 0)`.
 *                              Allows the engine to distinguish "fully
 *                              alive" (alive==1) from "alive-in-substate"
 *                              (alive==2 + alias_24 set).
 *   hit_history[10]    +0x778..0x79c  10-entry ring of slot SEQ_IDs that
 *                              recently hit this NPC.  Combat SM scans
 *                              this to skip "I've already been hit by
 *                              this attack-slot".  Engine writes via
 *                              `npc[hit_cursor + 0x15] = slot[0x47];
 *                               hit_cursor = (hit_cursor + 1) % 10`.
 *   hit_cursor         +0x7a0  ring write index, modulo 10.
 *
 * C8jb.3 (combat SM Phase B collision math) additions:
 *
 *   combat_pose[3]     +0x3e4/+0x3e8/+0x3ec  primary NPC attack pose,
 *                              distinct from `pos` at +0x00.  Combat SM
 *                              reads engine `npc[-0x1c9..-0x1c7]`.
 *                              Engine writers (per-frame NPC AI, not
 *                              yet ported) keep this synced with `pos`
 *                              for most NPCs; the duality matters when
 *                              the AI applies an attack-pose offset.
 *   npc_type           +0x424  NPC type ID.  Drives Phase B sub-iter
 *                              count: 7 for 0x44/0x45 (multi-hit), 2
 *                              for 0x46/0x47 (paired-hit), else 1.
 *                              Also drives anchor table selection.
 *   attack_radius      +0xabc  attack radius scalar, halved when the
 *                              sub-iter uses an anchor pose (multi-hit
 *                              attacks have smaller radii per hit).
 *   anchors[8][3]      +0x6fc + iVar8*12  3-float multi-hit anchor
 *                              poses, indexed by iVar8 (from the
 *                              rdata DAT_005c530c / DAT_005c5314
 *                              tables).  Generous 8-entry cap; engine
 *                              tables index up to 7.
 *
 * C8jb.4 (combat SM Phase B angle filter) additions:
 *
 *   npc_yaw            +0x420  NPC facing yaw (radians).  Combat SM's
 *                              0x44/0x45 angle filter computes
 *                              `atan2(dx, dz) - npc_yaw + π` normalized
 *                              into [-π, π]; cancels hit if |angle| ≥
 *                              0.9424779 (≈54° from facing).
 *   npc_phase          +0xa54  NPC combat phase (engine `local_30[-0x2d]`).
 *                              When phase==6 AND subphase==1, the
 *                              0x44/0x45 angle filter is bypassed
 *                              (collision stays armed regardless of
 *                              facing).
 *   npc_subphase       +0xa5c  NPC combat sub-phase (engine
 *                              `local_30[-0x2b]`).
 *
 * The struct's field layout no longer mirrors engine byte offsets — it's
 * a host-side decomposition.  All readers/writers go through named
 * fields, so the layout drift is invisible.
 */
typedef struct {
    float   pos[3];
    float   target[3];
    int32_t alive;
    int32_t action;
    int32_t state_counter;
    int32_t cooldown;
    int32_t sister_720;
    int32_t sister_724;
    int32_t combat_cooldown_5;
    int32_t alive_alias_24;
    int32_t hit_history[10];
    int32_t hit_cursor;
    float   combat_pose[3];
    int32_t npc_type;
    float   attack_radius;
    float   anchors[8][3];
    float   npc_yaw;
    int32_t npc_phase;
    int32_t npc_subphase;

    /* ─── C8jb.5a fields (combat-SM damage-roll prologue) ─────────────── */
    /*
     * `npc_b18_kill_age_out` — engine byte +0x734 (asm `esi+0xb18`).
     * Written by the slot TYPE==0x53 heavy-attack short-circuit with
     * `MAX(0, kill_age - slot.AGE)`.  kill_age comes from the
     * FUN_004319d6 stage-transition hook (0x78 ticks during transition,
     * 600 ticks otherwise).  No in-port consumer reads it yet.
     */
    int32_t npc_b18_kill_age_out;

    /* ─── C8jb.5b fields (combat-SM general damage formula) ────────────── */
    /*
     * `damage_quirk_mul_ab8` — engine byte +0xab8 (asm `esi+0xab8`).
     * Float multiplier read twice (once per pass) and squared (`fmul mem;
     * fmul mem`) in the npc-quirk path of the damage formula.  Production
     * writer not yet identified; tests inject a non-zero value to verify
     * the npc-quirk contribution to damage.
     *
     * `damage_quirk_disable_b28` — engine byte +0xb28 (asm `esi+0xb28`).
     * Int gate: when non-zero, BOTH npc-quirk paths zero out (the int is
     * AND'd with 0 then re-converted to float).  Effectively disables the
     * npc.damage_quirk_mul_ab8² contribution to both passes.
     *
     * `block_dodge_b38` — engine byte +0xb38 (asm `esi+0xb38`, =
     * decomp `piVar7[0xc]`).  Int counter: when > 0, halves the final
     * damage (signed-shift `/2`).  Survey doc names this "block/dodge
     * counter" — likely incremented by an NPC's block/dodge AI when it
     * successfully defends, then decremented by some unported timer.
     */
    float   damage_quirk_mul_ab8;
    int32_t damage_quirk_disable_b28;
    int32_t block_dodge_b38;

    /* ─── C8jb.5c fields (combat-SM post-damage clamps) ─────────────────── */
    /*
     * `charge_flag` — engine byte +0xb9c (asm `[esi+0xb9c]`).  Int — when
     * non-zero AND npc.npc_b18_kill_age_out == 0, the SM evaluates the
     * "charge-attack into facing" path: if |atan2(dx,dz) - npc_yaw + π|
     * normalized is < 0.3π (~63°), the collision is disarmed (damage → 0)
     * and the NPC's combat phase resets to 4.
     *
     * `npc_phase_counter1` — engine byte +0xa58 (asm `[esi+0xa58]`).  Int
     * — zeroed when (a) the 0x44/0x45 + slot.TYPE 0x12 NPC reset fires AT
     * sub_iter 0, or (b) the charge-attack disarm fires.  Otherwise
     * unchanged.  No reader in C8jb.5c (downstream uses by C8jb.6+).
     *
     * `npc_phase_counter2` — engine byte +0xa60 (asm `[esi+0xa60]`).  Int
     * — zeroed only by the 0x44/0x45 + slot.TYPE 0x12 NPC reset (with
     * sub_iter == 0).  No reader yet.
     */
    int32_t charge_flag;
    int32_t npc_phase_counter1;
    int32_t npc_phase_counter2;
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
