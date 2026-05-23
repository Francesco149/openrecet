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

/* Implemented as of C8i.2:
 *   C8i.1 anchors — 0x60 (no-op reservation), 0x20 (age=0),
 *                   0x66 (vel=(0,0,-1) + random life cap).
 *   C8i.2 radial bursts — 1/2/3/0x52/0x5e/0x65 (8-particle group A),
 *                         0x92 (1-particle color-cycle burst),
 *                         0x79 (128-particle swarm w/ AGE stagger),
 *                         0x5d (45-particle swarm w/ AGE=-i, PARAM1=-i).
 * Unimplemented types record a trace but do not allocate or commit a
 * slot — they will become real spawns as C8i.3..5 land. */
#define SCENE1_SPAWN_TYPE_IMPLEMENTED(t)                                    \
    ((t) == 0x60 || (t) == 0x20 || (t) == 0x66 || (t) == 0x92 ||            \
     (t) == 1    || (t) == 2    || (t) == 3    || (t) == 0x52 ||            \
     (t) == 0x5e || (t) == 0x65 || (t) == 0x79 || (t) == 0x5d)

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
