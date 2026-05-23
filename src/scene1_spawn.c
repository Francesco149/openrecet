/*
 * scene1_spawn.c — see scene1_spawn.h for the chip writeup.
 *
 * Port of FUN_00447f4f @ 0x447f4f (1449-line Ghidra decomp).  C8i.1
 * implements the outer slot-scan + common preamble + 3 anchor type
 * handlers (0x60, 0x20, 0x66).  Other types record a trace then return
 * without committing a slot — they'll become real spawns as C8i.2..5
 * land.
 *
 * Translation conventions:
 *
 *   - Per-slot field access uses SCENE1_RECORDS_A_OFF_* (slot base =
 *     DAT_069b2f80; TYPE at +12 dw; etc.) — same as the integrator.
 *
 *   - thunk_FUN_005041f6() → rng_next15() (15-bit PRNG int).
 *
 *   - The engine's outer loop scans slot indices 0..4095 (`do { … }
 *     while(local_10 != 0x1000)`).  We mirror this exactly — the
 *     scanning order is observable via the resulting slot indices, which
 *     the integrator's later type-0x18 / type-0x4a handlers consume.
 *
 *   - The trace ring is recorded at the top of scene1_spawn() so the
 *     pre-C8i tests (which assert on the trace after type-0x21 calls)
 *     continue to pass while the per-type init body for 0x21 is unported.
 */

#include "scene1_spawn.h"

#include "rng.h"
#include "scene1_records.h"

int                 g_scene1_spawn_trace_count;
scene1_spawn_call_t g_scene1_spawn_trace[SCENE1_SPAWN_TRACE_CAPACITY];

/* Slot field helpers — float-bits stored in int slot.  Mirrors the
 * convention in scene1_particles_tick.c. */
static inline int32_t *slot_int(int i, int off)
{
    return &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
}

static inline void slot_set_f(int i, int off, float f)
{
    int32_t v;
    __builtin_memcpy(&v, &f, sizeof v);
    g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off] = v;
}

static inline int slot_type(int i)
{
    return g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE
                              + SCENE1_RECORDS_A_OFF_TYPE];
}

/* ─── common preamble (engine lines 33-46) ──────────────────────────
 *
 * Writes 11 fields the engine sets unconditionally for every committed
 * slot, regardless of type.  After this returns, slot[i].TYPE == type,
 * so the outer-loop sentinel check (TYPE != -1 → skip) sees this slot
 * as busy on later iterations within the same call. */
static void commit_slot_preamble(int i, int slot_hint, float x, float y,
                                 float z, int type, float scale)
{
    *slot_int(i, SCENE1_RECORDS_A_OFF_AUX_18) = slot_hint;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, z);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 0.0f);
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = 0;
    *slot_int(i, SCENE1_RECORDS_A_OFF_TYPE)   = type;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_SCALE, scale);
}

/* ─── per-type init: 0x60 — pure no-op (engine L611) ─────────────────
 *
 * Engine reaches LAB_0044a997 directly (bVar11 = local_8 == 0; return
 * after 1 particle).  No extra writes beyond the preamble. */
static void init_type_60(int i)
{
    (void)i;   /* preamble is the entire body */
}

/* ─── per-type init: 0x20 — explicit age=0 (engine L600-L606) ────────
 *
 * Writes age = 0 (already zero from preamble; the engine writes it
 * anyway because labels LAB_0044a994 / LAB_0044a997 are shared entry
 * points for types 0x41/0x61/0x62/0x72/0x20 which DO want the age
 * reset).  Returns after 1 particle. */
static void init_type_20(int i)
{
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = 0;
}

/* ─── per-type init: 0x66 — zero-vel-down + random life (engine L177-184) ──
 *
 *   vel = (0, 0, -1.0f)            // 0xbf800000
 *   PARAM1 = rng_next15() % 100 + 20   // random life cap 20..119
 *   age = 0   (redundant — preamble already zeroed it)
 *
 * Returns after 1 particle (LAB_0044a985 → bVar11 = local_8 == 0). */
static void init_type_66(int i)
{
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, -1.0f);
    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;
}

/* ─── dispatch: returns how many slots to commit for `type`.
 *
 * All C8i.1 anchor types spawn 1.  Higher counts (8 for type 1, 12 for
 * the mega-group, 128 for 0x79, etc.) land in C8i.2-5.  See
 * scene1-spawn.md for the full table. */
static int spawn_count_for_type(int type)
{
    switch (type) {
    case 0x60: return 1;
    case 0x20: return 1;
    case 0x66: return 1;
    default:   return 0;   /* unimplemented — record trace only */
    }
}

/* Per-type init dispatch.  Called once per committed slot. */
static void run_type_init(int type, int i)
{
    switch (type) {
    case 0x60: init_type_60(i); break;
    case 0x20: init_type_20(i); break;
    case 0x66: init_type_66(i); break;
    default: break;
    }
}

void scene1_spawn(int slot_hint, float x, float y, float z, int type,
                  float scale, int param7)
{
    /* Trace first — preserves the pre-C8i debug API regardless of
     * whether the requested type has been ported yet. */
    int trace_idx = g_scene1_spawn_trace_count % SCENE1_SPAWN_TRACE_CAPACITY;
    scene1_spawn_call_t *c = &g_scene1_spawn_trace[trace_idx];
    c->slot_hint = slot_hint;
    c->x = x;
    c->y = y;
    c->z = z;
    c->type = type;
    c->scale = scale;
    c->param7 = param7;
    g_scene1_spawn_trace_count++;

    int want = spawn_count_for_type(type);
    if (want == 0) {
        /* Unimplemented type — do not commit a slot.  When C8i.2..5
         * land, this branch shrinks and eventually disappears.
         *
         * NOTE: the engine for an unknown type commits a slot via the
         * preamble then falls through to the slot-scan continuation,
         * leaking the slot.  We diverge here for safety until all types
         * are covered, then will match the engine. */
        return;
    }

    /* Mirror the engine's outer loop: scan slots 0..4095 looking for
     * sentinel-empty (TYPE == -1).  Commit each free slot we find,
     * running per-type init; stop after `want` commits. */
    int got = 0;
    for (int local_10 = 0; local_10 < SCENE1_RECORDS_A_COUNT; local_10++) {
        if (slot_type(local_10) != -1) continue;

        commit_slot_preamble(local_10, slot_hint, x, y, z, type, scale);
        run_type_init(type, local_10);
        (void)param7;   /* C8i.1 anchor types don't read param7 — used
                         * by 0x4a / 0x12 / 0x78 / 0x21 in later chips */
        got++;
        if (got == want) return;
    }
    /* Fell off the end (table exhausted) — engine returns silently. */
}

void scene1_spawn_trace_reset(void)
{
    g_scene1_spawn_trace_count = 0;
    for (int i = 0; i < SCENE1_SPAWN_TRACE_CAPACITY; i++) {
        g_scene1_spawn_trace[i].slot_hint = 0;
        g_scene1_spawn_trace[i].x = 0.0f;
        g_scene1_spawn_trace[i].y = 0.0f;
        g_scene1_spawn_trace[i].z = 0.0f;
        g_scene1_spawn_trace[i].type = 0;
        g_scene1_spawn_trace[i].scale = 0.0f;
        g_scene1_spawn_trace[i].param7 = 0;
    }
}

int                     g_scene1_mesh_emit_trace_count;
scene1_mesh_emit_call_t g_scene1_mesh_emit_trace[SCENE1_MESH_EMIT_TRACE_CAPACITY];

int scene1_pick_mesh_id(void)
{
    /* FUN_004385fb is a small mesh-table lookup with PRNG — stable 0
     * is fine for the stub; the C8h.4b consumer (type 0x6e) only feeds
     * the result back into scene1_mesh_emit. */
    return 0;
}

void scene1_mesh_emit(float x, float y, float z, int mesh_id, int slot,
                      int param7)
{
    int idx = g_scene1_mesh_emit_trace_count % SCENE1_MESH_EMIT_TRACE_CAPACITY;
    scene1_mesh_emit_call_t *c = &g_scene1_mesh_emit_trace[idx];
    c->x = x;
    c->y = y;
    c->z = z;
    c->mesh_id = mesh_id;
    c->slot = slot;
    c->param7 = param7;
    g_scene1_mesh_emit_trace_count++;
}

void scene1_mesh_emit_trace_reset(void)
{
    g_scene1_mesh_emit_trace_count = 0;
    for (int i = 0; i < SCENE1_MESH_EMIT_TRACE_CAPACITY; i++) {
        g_scene1_mesh_emit_trace[i].x = 0.0f;
        g_scene1_mesh_emit_trace[i].y = 0.0f;
        g_scene1_mesh_emit_trace[i].z = 0.0f;
        g_scene1_mesh_emit_trace[i].mesh_id = 0;
        g_scene1_mesh_emit_trace[i].slot = 0;
        g_scene1_mesh_emit_trace[i].param7 = 0;
    }
}
