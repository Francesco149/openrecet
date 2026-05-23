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

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented as of C8i.4 (mega-group + full C8i.3 ladder landed):
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
 * Unimplemented types record a trace but do not allocate or commit a
 * slot — they will become real spawns as C8i.5 lands. */
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
