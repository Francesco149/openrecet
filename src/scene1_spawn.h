/*
 * scene1_spawn.h — scene-1 particle spawn API (FUN_00447f4f).
 *
 * Chip C8i.1 (2026-05-23).  Real port of the per-type spawn dispatcher
 * begins here.  See `docs/findings/scene1-spawn.md` for the full ladder
 * and the table-A writer-view column map.
 *
 * Engine signature (Ghidra-recovered):
 *
 *   FUN_00447f4f(int param_1, float x, float y, float z, int type,
 *                float scale, int param_7);
 *
 * param_1 is a "slot hint" (callers pass 0 for "any free slot") stored
 * in dword 18 of the allocated slot — engine consumers do not appear to
 * read it back.  scale (param_6) is the float written to the slot's
 * SCALE field; param_7 is per-type scratch written into PARAM1 or aux_15
 * by some types.
 *
 * Per-call effect: scan table A for a sentinel-empty slot; if found,
 * commit the slot (write common preamble), then run a per-type init
 * body.  Some types loop and commit several slots before returning (see
 * scene1-spawn.md, "Per-type loop count").  C8i.1 only implements 3
 * anchor types — see SCENE1_SPAWN_TYPE_IMPLEMENTED().
 *
 * The trace ring buffer below is kept as opt-in instrumentation — every
 * call records its args regardless of whether the type's init body is
 * implemented yet.  Pre-C8i tests that asserted on the trace still pass.
 */
#ifndef SCENE1_SPAWN_H
#define SCENE1_SPAWN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented as of C8i.5c (camera-yaw trig + aux_15 flag — 9 types
 * added on top of C8i.5b, closes the ladder):
 *   C8i.5c camera-yaw trig (9 types):
 *      - 0x15: cube vel + AGE stagger; SCALE-driven; no camera trig.
 *      - 0x16: cube vel + world-radial xz pos jitter + pos.y lift.
 *      - 0x18: vel.x positive bias + inner-angle pos jitter (u for
 *        pos.y BETWEEN sin/cos) + sin(-yaw)*5/cos(-yaw)*5 bend +
 *        pos -= vel*60 anchor-back.  AGE = -i.
 *      - 0x58: wider amplitudes than 0x18; pos -= vel*200; PARAM2 =
 *        400 with 50/50 chance to fold pos += 400*vel + flip vel.y +
 *        drop PARAM2 to 200.  NO AGE write (preamble 0 stands).
 *      - 0x4f: sin/cos(π/2) constant-angle vel → vel.x = mag, vel.z =
 *        0; inner-angle pos jitter same as 0x18; pos -= vel*100;
 *        PARAM2 = 100.
 *      - 0x3f / 0x56: cube xz pos jitter + sin/cos(-yaw)*15 bend +
 *        positive vy; SCALE unused.
 *      - 0x10 / 0x91: cube vel w/ vy ≈ 0.98 bias; PARAM1 = (rng15() &
 *        0xf) - 2 signed; if (param_7 < 10) vel.y *= 0.7, aux_15 = 0
 *        else aux_15 = 1.  This is the only C8i.5c handler that writes
 *        the aux_15 slot field.
 *
 * All 9 C8i.5c types are param_7-count (LAB_0044aa47).  Camera-yaw
 * reads use the existing g_scene1_camera_yaw (engine _DAT_073de39c),
 * already exported from scene1_particles_tick.h.
 *
 * Earlier C8i (as of C8i.5b — param_7-count radials + 8-particle cube,
 * 15 types added on top of C8i.5a):
 *   C8i.1 anchors — 0x60 (no-op reservation), 0x20 (age=0),
 *                   0x66 (vel=(0,0,-1) + random life cap).
 *   C8i.2 radial bursts — 1/2/3/0x52/0x5e/0x65 (8-particle group A),
 *                         0x92 (1-particle color-cycle burst),
 *                         0x79 (128-particle swarm w/ AGE stagger),
 *                         0x5d (45-particle swarm w/ AGE=-i, PARAM1=-i).
 *   C8i.3a world-anchored radial variants — 0x69 (128-particle, wider
 *                         mag), 0x68 (1, anchor-back -48× w/ swapped
 *                         cos→vy), 0x73+0x77 (2, vel-down + param7-trig
 *                         + world-jitter pos), 99 (1, anchor-back -40×
 *                         w/ BASE set), 0x78 (same as 99 + PARAM2=param7).
 *   C8i.3b mixed-shape multi-particle radials — 0x53 (1, world-radial
 *                         xz + positive vy), 0x4a (8, matrix-init w/
 *                         PARAM1=param7 + AGE=i*-4), 0x43 (24, ring-
 *                         from-below w/ rot.y = angle), 0x97 (64,
 *                         spherical w/ scale*=(u+0.5)/2), 0x96 (64,
 *                         spherical + camera-angle xz bend w/ scale*=
 *                         (u+0.5)), 0x40 (8, equal-spaced ring w/
 *                         azimuth = i*2π/8), 0x36+0x74 (param_7-count,
 *                         re-scaled SCALE), 0x4e (3, narrowed vel +
 *                         wide pos).
 *   C8i.3c local_8-azimuth + chain pair — 0x34 (24, chain-spawner w/
 *                         vel dead-write + final vel.y/z = rot seeds +
 *                         pos = param - vel*24), 0x35 (1, chain target
 *                         w/ rot only), 0x2c (32, small radial w/ AGE
 *                         = -i), 0x29 (14, raw vel without scale,
 *                         no pos write), 0x32 (2, π/2 string), 0x4c +
 *                         0x55 (1, π/4 string w/ alternating vel.x
 *                         sign + scale decay), 0x4b (3, count-driven
 *                         vel.x version of 0x4c), 0x33+0x4d+0x51
 *                         (param_7-count strings w/ random rot.x),
 *                         0x57 (1, param_7-sign rot.z + PARAM2=param7),
 *                         0x3e (4, rot.z jitter string).
 *   C8i.3d orbit/fountain/world-jitter exotics — 0x3d (20, string +
 *                         BASE), 0x6d/0x45 (param_7, camera-yaw
 *                         fountain reads g_scene1_camera_yaw_alt),
 *                         0x6c (1, all-zero noop), 0x6e (1, waypoint
 *                         homing vel=(target-pos)/100), 0x1f/100 (1,
 *                         scene-counter wave reads new
 *                         g_scene1_spawn_scene_counter_dab58), 0x23 (1,
 *                         preamble-only return), 0x22/0x3c/0x5a/0x2d
 *                         (20, world-jitter via BASE or pos+=offset),
 *                         0x1d (1, scattered cube vel + rot.z + PARAM2
 *                         = param_7).
 *   C8i.4 line-1240 mega-group — one generic-scatter body shared by 34
 *                         types: 0x25-0x28, 0x37-0x3a, 0x46-0x49,
 *                         0x7a-0x84, 0x86-0x90.  Each commits 12
 *                         particles.  Effect: small ground-skew vel,
 *                         world-radial pos jitter, alternating rot.y
 *                         wobble, 10-color cycle in PARAM2.
 *   C8i.5a one-particle tails — 23 short-bodied types that share a few
 *                         epilogue label patterns in the engine
 *                         (LAB_0044ad44 / ad57 / ad6a / ad72 / ad77):
 *      - preamble-only: 0x19, 0x44, 0x94, 0x2e, 0x1e
 *      - PARAM2 = param_7: 0x1a (shares engine LAB_0044ad44 with 0x1d)
 *      - rot.z = u*2π: 0x5f, 4, 0x70, 0x1c (LAB_0044ad57)
 *      - rot.y = u*2π + rot.z = u*2π: 0x42 (falls through ad49→ad57)
 *      - PARAM1 = param_7: 0x2a, 0x13, 0x14 (LAB_0044ad72)
 *      - PARAM1 = param_7 + scene_arm_24 side-effect: 0x24
 *      - PARAM2 = sim-state (g_scene1_spawn_global_ae84): 6, 7, 8, 9
 *      - const-vel + AGE = -(u%24): 0x11
 *      - AGE=0 + PARAM1=param_7 + PARAM2=0: 0x12, 0x54
 *      - slot_hint scratch copy + const-vel + AGE: 0x50
 *   C8i.5b param_7-count + 8-particle cube (15 types):
 *      - sin(angle)*u*0.5 + (u-0.5)*0.5 vy + cos(angle)*u*0.5 +
 *        rot.z=u*2π shared body (LAB_0044a43d) — 5, 0x5c, 0x6f, 10,
 *        0xb, 0xc, 0xe, 0x2b, 0x1b, 0x3b, 0x76 (param_7 particles each).
 *      - 0x67: same shared body but vy = (u+0.1)*0.5 (positive bias).
 *      - 0x59: anchor-back radial — mag=(u+2)*0.3, vy=(u+0.5)*0.2,
 *        pos.xz -= vel.xz * 20, pos.y += 0.5; no scale factor anywhere.
 *      - 0x71: centered radial — fVar1=2(u+0.2), vel scales by SCALE/2,
 *        vy = u*SCALE*1.25; PARAM1=u%100+0x14 life cap, AGE=-i stagger.
 *      - 0xf: 8-particle cube — vel.x/z = (u-0.5)*3.2, vel.y =
 *        (u+0.1)*0.8, AGE=-i; consumes one dead rng_next15 between
 *        vel.z and AGE.  Unique among C8i.5b in NOT using param_7.
 * C8i.5c closes the spawn ladder — all ~134 per-type handlers in
 * FUN_00447f4f are now covered.  Any future "unimplemented" entries
 * would be types the survey missed.  No known gaps as of 2026-05-23. */
#define SCENE1_SPAWN_TYPE_IMPLEMENTED(t)                                    \
    ((t) == 0x60 || (t) == 0x20 || (t) == 0x66 || (t) == 0x92 ||            \
     (t) == 1    || (t) == 2    || (t) == 3    || (t) == 0x52 ||            \
     (t) == 0x5e || (t) == 0x65 || (t) == 0x79 || (t) == 0x5d ||            \
     (t) == 0x69 || (t) == 0x68 || (t) == 0x73 || (t) == 0x77 ||            \
     (t) == 99   || (t) == 0x78 || (t) == 0x53 || (t) == 0x4a ||            \
     (t) == 0x43 || (t) == 0x97 || (t) == 0x96 || (t) == 0x40 ||            \
     (t) == 0x36 || (t) == 0x74 || (t) == 0x4e || (t) == 0x34 ||            \
     (t) == 0x35 || (t) == 0x2c || (t) == 0x29 || (t) == 0x32 ||            \
     (t) == 0x4c || (t) == 0x55 || (t) == 0x4b || (t) == 0x33 ||            \
     (t) == 0x4d || (t) == 0x51 || (t) == 0x57 || (t) == 0x3e ||            \
     (t) == 0x3d || (t) == 0x6d || (t) == 0x45 || (t) == 0x6c ||            \
     (t) == 0x6e || (t) == 0x1f || (t) == 100  || (t) == 0x23 ||            \
     (t) == 0x22 || (t) == 0x3c || (t) == 0x5a || (t) == 0x2d ||            \
     (t) == 0x1d ||                                                         \
     /* C8i.5a one-particle tails (23 types) */                             \
     (t) == 0x19 || (t) == 0x44 || (t) == 0x94 || (t) == 0x2e ||            \
     (t) == 0x1e || (t) == 0x1a || (t) == 0x5f || (t) == 4    ||            \
     (t) == 0x70 || (t) == 0x1c || (t) == 0x42 || (t) == 0x2a ||            \
     (t) == 0x13 || (t) == 0x14 || (t) == 0x24 || (t) == 6    ||            \
     (t) == 7    || (t) == 8    || (t) == 9    || (t) == 0x11 ||            \
     (t) == 0x12 || (t) == 0x54 || (t) == 0x50 ||                           \
     /* C8i.5b param_7-count + 8-cube (15 types) */                         \
     (t) == 5    || (t) == 0x5c || (t) == 0x6f || (t) == 10   ||            \
     (t) == 0xb  || (t) == 0xc  || (t) == 0xe  || (t) == 0x2b ||            \
     (t) == 0x1b || (t) == 0x3b || (t) == 0x76 || (t) == 0x67 ||            \
     (t) == 0x59 || (t) == 0x71 || (t) == 0xf  ||                           \
     /* C8i.5c camera-yaw / aux_15 flag (9 types) */                        \
     (t) == 0x15 || (t) == 0x16 || (t) == 0x18 || (t) == 0x58 ||            \
     (t) == 0x4f || (t) == 0x3f || (t) == 0x56 || (t) == 0x10 ||            \
     (t) == 0x91 ||                                                         \
     /* C8i.4 mega-group ranges (34 types, all share init_type_mega_group) */ \
     ((t) >= 0x25 && (t) <= 0x28) ||                                        \
     ((t) >= 0x37 && (t) <= 0x3a) ||                                        \
     ((t) >= 0x46 && (t) <= 0x49) ||                                        \
     ((t) >= 0x7a && (t) <= 0x84) ||                                        \
     ((t) >= 0x86 && (t) <= 0x90))

/* Stand-in for type 0x96's camera-angle bend.  Engine reads
 * `*(int *)(slot_hint + 0x948)` directly (slot_hint is overloaded as a
 * scene-state pointer for 0x96 callers).  Until the sim-side caller
 * ports, tests set this global to drive the trig dependency.  Default 0
 * → sin(0)=0, cos(0)=1, so vel.z gets a constant +0.2 bend when unset. */
extern int g_scene1_spawn_camera_counter_948;

/* Stand-in for types 6/7/8/9's PARAM2 seed.  Engine reads
 * `DAT_056dae84` directly (a small sim-scene int set by the unported
 * FUN_00436f97 — same writer family as DAT_056dab58 below).  Default 0
 * makes the 6..9 swarm orbit start at phase 0.  Tests set it to verify
 * the snapshot path. */
extern int g_scene1_spawn_global_ae84;

/* Stand-ins for type 0x50's slot_hint-as-pointer derefs.  Engine reads:
 *   - `*(uint *)(slot_hint + 0xd04 + k*4)` for k in 0..16  → copied
 *     verbatim into slot scratch dw 20..36 (byte offset 0x50..0x90).
 *   - `*(uint *)(slot_hint + 0xea4)`                       → rot.y.
 * slot_hint is overloaded as a scene-state pointer for 0x50 callers
 * (only the unported sim caller passes a real pointer; chain-spawn
 * passes 0).  Stand-ins default to 0 so the dormant case stays benign;
 * tests set them to verify the copy + the rot.y store. */
extern int32_t g_scene1_spawn_50_block_d04[17];
extern int32_t g_scene1_spawn_50_rot_y_ea4;

/* Counts type-0x24's side-effect calls (engine push 0xffffffff; call
 * 0x40c90e — sets DAT_0064828c=1 and DAT_00529900=-1).  Neither scene-
 * side flag is referenced by any particle path in the port, so the
 * counter is the observable proxy that the arm-flag fired. */
extern int g_scene1_spawn_type_24_arm_count;

/* Stand-in for types 0x1f/100's scene-counter wave.  Engine reads
 * `DAT_056dab58` (a small int set by the sim caller FUN_00436f97 — see
 * its lines 680/685 for assignments to 2/4).  Default 0 → sin(0)=0,
 * cos(0)=1, so pos.x gets a constant `(u-0.5) + x` offset and pos.z
 * collapses to z.  Tests set this to verify the trig dependency. */
extern int g_scene1_spawn_scene_counter_dab58;

/*
 * Trace ring — kept for tests + debug instrumentation.  Reset by
 * scene1_spawn_trace_reset().  Records every call to scene1_spawn() in
 * order; oldest entries overwrite when count > capacity.
 */
#define SCENE1_SPAWN_TRACE_CAPACITY 32

typedef struct {
    int   slot_hint;
    float x, y, z;
    int   type;
    float scale;
    int   param7;
} scene1_spawn_call_t;

extern int                 g_scene1_spawn_trace_count;
extern scene1_spawn_call_t g_scene1_spawn_trace[SCENE1_SPAWN_TRACE_CAPACITY];

void scene1_spawn(int slot_hint, float x, float y, float z, int type,
                  float scale, int param7);

void scene1_spawn_trace_reset(void);

/*
 * Stub for FUN_0044b0f3 — the engine's "emit one short-lived mesh
 * effect" call.  Distinct from scene1_spawn (which writes into the
 * particle table); this one drops a mesh into a separate "mesh effects"
 * container at DAT_056da1b8.  The only caller in C8h.4b is the type
 * 0x6e handler, which emits a mesh at tick=100 then continues as a
 * particle until age=0x74.
 *
 * Engine signature (Ghidra-recovered):
 *   FUN_0044b0f3(void *container, float x, float y, float z,
 *                int mesh_id, int slot, int param_7);
 * where mesh_id comes from FUN_004385fb() — a tiny mesh-ID picker also
 * stubbed here for the integrator.  Replace both when the mesh-effect
 * emitter ports as its own chip.
 */
#define SCENE1_MESH_EMIT_TRACE_CAPACITY 8

typedef struct {
    float x, y, z;
    int   mesh_id;
    int   slot;
    int   param7;
} scene1_mesh_emit_call_t;

extern int                     g_scene1_mesh_emit_trace_count;
extern scene1_mesh_emit_call_t g_scene1_mesh_emit_trace[SCENE1_MESH_EMIT_TRACE_CAPACITY];

/* Picks a mesh ID for the chained emit.  Engine FUN_004385fb is a
 * randomized table lookup — stub returns 0 so tests get a stable value. */
int  scene1_pick_mesh_id(void);

void scene1_mesh_emit(float x, float y, float z, int mesh_id, int slot,
                      int param7);

void scene1_mesh_emit_trace_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_SPAWN_H */
