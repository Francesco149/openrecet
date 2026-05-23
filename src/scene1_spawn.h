/*
 * scene1_spawn.h — stub for FUN_00447f4f (scene-1 particle spawn API).
 *
 * Chip C8h.1 (2026-05-23).  Provides a placeholder for the spawn API
 * the particle integrator chains into when handlers like type 0x20
 * (player-snap; every 4 ticks chain-spawns type 0x21) and type 0x1a
 * (anchor-snap; on kill chain-spawns type 1) call the engine's
 * FUN_00447f4f.
 *
 * The real port is the C8i chip (11826 B of decomp — single
 * allocate-slot scan + per-type init dispatch).  This stub records the
 * call (slot count + last args) so tests can verify the chained-spawn
 * was triggered, but does not allocate a slot or initialize one.
 *
 * Once C8i lands, this header stays; scene1_spawn() becomes the real
 * entry point and the trace accumulators stay around for tests + opt-in
 * debug instrumentation.
 *
 * Engine signature (Ghidra-recovered):
 *
 *   FUN_00447f4f(int param_1, float x, float y, float z, int type,
 *                float scale, int param_7);
 *
 * param_1 appears to be a "hint slot index" or "force this slot";
 * callers pass 0 for "pick any free slot" and -1 for "use the explicit
 * slot in param_7".  We don't decode that yet — recorded verbatim.
 *
 * scale + param_7 are optional; the integrator's chained-spawn calls
 * pass only (param_1, x, y, z, type) per the Ghidra signature — Ghidra
 * dropped the trailing optional args because the FPU/stack-arg flow
 * was hidden by float-on-stack conventions.  We accept the full 7-arg
 * form here and let chained-spawn callers pass 0 / 1.0f / 0 for the
 * missing tail.
 */
#ifndef SCENE1_SPAWN_H
#define SCENE1_SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Call records — kept for tests / debug instrumentation.  Reset by
 * scene1_spawn_trace_reset().  The buffer is small (32 entries) and
 * wraps; for the real integrator that fires hundreds of spawns per
 * tick this is "last-32" only, not a full audit log.
 */
#define SCENE1_SPAWN_TRACE_CAPACITY 32

typedef struct {
    int   slot_hint;
    float x, y, z;
    int   type;
    float scale;
    int   param7;
} scene1_spawn_call_t;

extern int                 g_scene1_spawn_trace_count;  /* total calls since reset */
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
