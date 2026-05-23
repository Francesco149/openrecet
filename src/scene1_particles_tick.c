/*
 * scene1_particles_tick.c — see scene1_particles_tick.h for the chip
 * writeup.
 *
 * Port of FUN_0040fb3a @ 0x40fb3a (1249-line Ghidra decomp).  C8h.1
 * implements the outer-loop dispatch + 4 of the ~95 type handlers
 * (camera-orbit 6/7/8/9, player-snap 0x20, cone-spread 0x21).
 *
 * Translation conventions:
 *
 *   - Per-slot field access uses the SCENE1_RECORDS_A_OFF_* offsets
 *     from scene1_records.h (slot base = DAT_069b2f80; TYPE at +12 dw;
 *     etc.).  The engine's Ghidra dump frequently writes through
 *     DAT_069b2fb0 (= slot base + 12 dw = TYPE address) — that's the
 *     same memory, accessed via a different name.
 *
 *   - x87 FPU sin/cos thunks (FUN_00503a44 / 00503994) → sinf/cosf
 *     from <math.h>.  The engine quirks doc has the accepted "tiny
 *     numeric divergence" note.
 *
 *   - Ghidra missed the FPU-stack arg on a few sin/cos calls (e.g.
 *     line 1120 of the type-6..9 handler).  Where the missing arg
 *     can't be reconstructed from the surrounding `local_*`
 *     assignments, the port uses the most-recently-loaded scalar
 *     (typically `age` or a derived float) and flags the call in
 *     `docs/findings/scene1-particles-tick.md` as needing Frida
 *     validation.
 *
 *   - Spawn API (FUN_00447f4f) is stubbed via scene1_spawn.c —
 *     chained-spawn calls (type 0x20 → 0x21, type 0x1a → 1, type 0x34
 *     → 0x35) record into the trace ring but don't allocate a slot.
 *     The C8i chip will swap the stub for the real port.
 */

#include "scene1_particles_tick.h"

#include <math.h>
#include <stdint.h>

#include "math3d.h"
#include "scene1_records.h"
#include "scene1_spawn.h"

/* Engine-global stubs — see header.  Writable so tests + downstream
 * ports can populate.  BSS-zero default mirrors the engine state at
 * INGAME entry today. */
float g_scene1_player_pos[3];
float g_scene1_spawn_origin[3];
int   g_scene1_scene_alive;
float g_scene1_camera_yaw;
float g_scene1_camera_anchor[2];

/* Slot field helpers — TYPE/age are int, everything else is float bits
 * stored in the int slot.  These are local-only to keep call sites
 * close to the engine decomp shape. */
static inline int32_t *slot_int(int i, int off)
{
    return &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
}

static inline float slot_get_f(int i, int off)
{
    int32_t v = g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
    float f;
    /* type-pun int bits → float bits */
    __builtin_memcpy(&f, &v, sizeof f);
    return f;
}

static inline void slot_set_f(int i, int off, float f)
{
    int32_t v;
    __builtin_memcpy(&v, &f, sizeof v);
    g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off] = v;
}

static inline int slot_type(int i)  { return g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + SCENE1_RECORDS_A_OFF_TYPE]; }
static inline int slot_age(int i)   { return g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + SCENE1_RECORDS_A_OFF_AGE]; }

static inline void slot_kill(int i) { *slot_int(i, SCENE1_RECORDS_A_OFF_TYPE) = -1; }
static inline void slot_age_inc(int i) { *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) += 1; }
static inline void slot_add_f(int i, int off, float dv) { slot_set_f(i, off, slot_get_f(i, off) + dv); }

/* ─── shared helpers for C8h.2 cookie-cutter handlers ────────────────
 *
 * Every "decay-drift" handler in FUN_0040fb3a follows one of a few
 * shapes:
 *
 *  decay_drift_uniform(damp, kill_age)
 *      pos += vel; vel *= damp (uniform across x/y/z); age++; kill at K.
 *
 *  decay_drift_grav_pre(damp, gravity_y, kill_age)
 *      pos += vel; vel.y += gravity_y; vel *= damp; age++; kill at K.
 *      (Gravity is applied BEFORE the damp.  Types 0x29, 0x1f, 100,
 *       0x36, 0x74, 0x4e use this.)
 *
 *  decay_drift_grav_post(damp, gravity_y, kill_age)
 *      pos += vel; vel *= damp; vel.y += gravity_y; age++; kill at K.
 *      (Gravity is applied AFTER the damp.  Types 0x96, 0x97 use this.)
 *
 *  scaled_drift_uniform(damp, kill_age)
 *      pos += vel * scale; vel *= damp; age++; kill at K.
 *      (Scale = SCALE field at slot+14.  Many types use this.)
 *
 *  scaled_drift_gated(damp, age_low_gate, kill_age)
 *      As scaled_drift_uniform but pos += only if age > age_low_gate
 *      (engine "if (-1 < age)" → age >= 0 gate, etc.).
 *
 *  age_only(kill_age)
 *      age++; kill at K.  Pure-age handlers like 0x44, 0x50.
 *
 *  field_decay_x(field_off, mul, kill_age)
 *      slot[field_off] += vel.x * mul; age++; kill at K.
 *      Mostly used to accumulate rot.x or rot.y from a "speed" stored
 *      in vel.x.
 *
 *  age_only_kill_at_param1_offset(extra)
 *      age++; kill at age == PARAM1 + extra.  Only type 0x5d uses this.
 *
 * All helpers below take a slot index plus the per-type constants and
 * return void; callers are expected to gate by type in the outer loop. */

static void decay_drift_uniform(int i, float damp, int kill_age)
{
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * damp);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * damp);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * damp);
    slot_age_inc(i);
    if (slot_age(i) == kill_age) slot_kill(i);
}

static void decay_drift_grav_pre(int i, float damp, float gravity_y, int kill_age)
{
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz);
    vy += gravity_y;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * damp);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * damp);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * damp);
    slot_age_inc(i);
    if (slot_age(i) == kill_age) slot_kill(i);
}

/* decay_drift_grav_post — not used by any landed handler yet; type
 * 0x96/0x97 (the only post-damp gravity types in C8h.2) need extra rot
 * bumps so they inline the math.  Will be useful when a later chip
 * finds a "pure" post-damp variant.  Keep the helper out of the build
 * for now (commented to avoid -Wunused-function). */

static void scaled_drift_uniform(int i, float damp, int kill_age)
{
    float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * damp);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * damp);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * damp);
    slot_age_inc(i);
    if (slot_age(i) == kill_age) slot_kill(i);
}

static void age_only_kill_at(int i, int kill_age)
{
    slot_age_inc(i);
    if (slot_age(i) == kill_age) slot_kill(i);
}

static void field_decay_x(int i, int field_off, float mul, int kill_age)
{
    float v = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    slot_add_f(i, field_off, v * mul);
    slot_age_inc(i);
    if (slot_age(i) == kill_age) slot_kill(i);
}

/* ─── per-frame open (FUN_00414929) ──────────────────────────────────
 *
 * 1465 B sibling that ticks two non-particle entity tables
 * (DAT_00730c30 stride 0xb and DAT_0064e8a0 stride 0x37, with embedded
 * FUN_00414345 / FUN_0040656e / FUN_005031e4 calls).  Provisional
 * no-op for C8h.1 — separate chip when those tables' consumers port.
 */
static void particles_per_frame_open(void)
{
    /* Intentionally empty.  See chip ladder in
     * docs/findings/scene1-particles-tick.md — this lands with a later
     * chip alongside the two entity tables. */
}

/* ─── handler: types 6, 7, 8, 9 — camera-orbit attract ───────────────
 *
 * Engine decomp L1097-L1129 (40fb3a.c).  All four types share one
 * body.  Shape: anchor A = camera-orbit start position; anchor B =
 * orbital target derived from PARAM2 (sector index ÷ 3 turns).
 * Interpolate pos from A → B over the first ~12 ticks, then snap.
 * Add a vertical sin bob to pos.y.  Kill when scene_alive == 0.
 *
 * Engine quirk: line 1120 in the decomp shows
 *   `fVar9 = (float10)FUN_00503a44();`
 * with NO argument — Ghidra lost the FPU-stack arg.  The most plausible
 * reconstruction (and the one used here) is `sinf(age_f)`, where age_f
 * is the just-loaded `(float)age` from L1114.  Verifying the exact
 * argument needs a Frida run against retail; flagged in
 * docs/findings/scene1-particles-tick.md → "Pending human checks".
 */
static void handle_type_6_to_9(int i)
{
    float yaw      = g_scene1_camera_yaw;
    float anchor_x = g_scene1_camera_anchor[0];
    float anchor_z = g_scene1_camera_anchor[1];
    float player_x = g_scene1_player_pos[0];
    float player_y = g_scene1_player_pos[1];
    float player_z = g_scene1_player_pos[2];

    /* Anchor A — camera-orbit start.  The 2.3876104 constant is the
     * decomp's literal at L1098/L1102 (≈ 3π/4 - some offset — likely
     * encoding the orbit's start sector). */
    float arg_a = 2.3876104f - yaw;
    float ax = sinf(arg_a) * 6.0f + anchor_x;     /* DAT_073de328 */
    float ay = player_y    + 1.0f;
    float az = cosf(arg_a) * 6.0f + anchor_z;     /* DAT_073de330 */

    /* Anchor B — per-sector orbit target.
     *   sector = PARAM2 (int)
     *   sector_angle = sector * 2π / 3 + π
     */
    int   sector = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2);
    float sector_angle = ((float)sector * 6.2831855f) / 3.0f + 3.1415927f;
    float arg_b = sector_angle - yaw;
    float bx = sinf(arg_b) * 1.5f + player_x;
    float by = player_y    + 0.5f;
    float bz = cosf(arg_b) * 1.5f + player_z;

    /* Interpolation factor: t = clamp(age * 0.08, 0, 1). */
    int   age   = slot_age(i);
    float age_f = (float)age;
    float t     = age_f * 0.08f;
    if (t > 1.0f) t = 1.0f;

    /* L1119: pos.x = lerp(anchor_a.x, anchor_b.x, t).
     * L1120: sin(?) — engine quirk; using sinf(age_f) per note above.
     * L1122-23: pos.y = lerp(a.y, b.y, t) + 2 * sin(?) + a.y_base.
     *
     * Decomp writes:
     *   pos.y = (b.y - a.y) * t + sin_result + sin_result + a.y
     * which simplifies to lerp(a.y, b.y, t) + 2*sin_result.  Verbatim. */
    float pos_x = (bx - ax) * t + ax;
    float sin_result = sinf(age_f);
    float pos_y = (by - ay) * t + sin_result + sin_result + ay;
    float pos_z = (bz - az) * t + az;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, pos_x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, pos_y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, pos_z);

    slot_age_inc(i);

    /* L1126: kill when scene_alive flag drops to 0. */
    if (g_scene1_scene_alive == 0) {
        slot_kill(i);
    }
}

/* ─── handler: type 0x20 — player-snap with every-4-tick chain-spawn ─
 *
 * Engine decomp L465-L475.  pos = (player.x, player.y + 2.5, player.z).
 * Age++.  If (age & 3) == 0 → spawn type 0x21 at current pos.
 * Kill when scene_alive == 0.
 *
 * Engine snap reads DAT_056da1f0/f4/f8 (spawn origin), not the player
 * pos at DAT_056da1d8/dc/e0.  These can differ — spawn origin is where
 * the player's particle effects emit from (often offset from the
 * player's render position for foot/hand effects).  Verbatim port.
 */
static void handle_type_20(int i)
{
    float ox = g_scene1_spawn_origin[0];
    float oy = g_scene1_spawn_origin[1];
    float oz = g_scene1_spawn_origin[2];

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, ox);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, oy + 2.5f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, oz);

    slot_age_inc(i);
    int age = slot_age(i);

    /* L470: `*(byte *)(&DAT_069b2fb4 + iVar4 * 0x25) & 3 == 0` — bit-
     * test the low 2 bits of age.  Every 4 ticks chains a 0x21 spawn
     * at the current snap position. */
    if ((age & 3) == 0) {
        float px = slot_get_f(i, SCENE1_RECORDS_A_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_A_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_A_OFF_POS_Z);
        /* Engine call (L471): FUN_00447f4f(0, x, y, z, 0x21) — Ghidra
         * decomp shows only 5 args; the spawn API's trailing scale +
         * param7 are float-on-stack and likely use compiler defaults.
         * Pass scale=1.0f, param7=0 as the safe MVP choice. */
        scene1_spawn(0, px, py, pz, 0x21, 1.0f, 0);
    }

    if (g_scene1_scene_alive == 0) {
        slot_kill(i);
    }
}

/* ─── handler: type 0x21 — cone-spread velocity sampling ─────────────
 *
 * Engine decomp L477-L504.  Snap pos to spawn origin (when PARAM2 !=
 * -1; PARAM2 holds a table-B reference index).  Bump rot.x by 0.15.
 * Zero vel.x.  vel.y = 0.2 - age.  Then compute angle from age and a
 * cone-spread offset from PARAM2 (referenced into table B's [0]
 * field for active/inactive).
 *
 * Kill conditions (L494-L503):
 *   - Always-kill at age == 0x20 (32 ticks).
 *   - If PARAM2 != -1 and the referenced table-B slot's [0] == 0
 *     (sentinel-empty), kill.
 *   - If PARAM2 == -1 and scene_alive == 0, kill.
 *
 * Engine quirk: line 487 in the decomp shows
 *   `local_8 = ((float)age * 0.7853982) / 32.0;
 *    fVar9 = FUN_00503994();  // cos — no arg`
 * Same Ghidra-FPU-stack pattern as types 6-9.  The most-recent FPU
 * assignment is local_8 (the angle); cosf(local_8) here.  This one is
 * less ambiguous — the missing arg is clearly the angle stored to
 * local_8 on the immediately prior line.
 */
static void handle_type_21(int i)
{
    int32_t param2 = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2);

    /* L478-L482: only snap to spawn origin if PARAM2 != -1.  When
     * PARAM2 == -1, the previous pos persists. */
    if (param2 != -1) {
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, g_scene1_spawn_origin[0]);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, g_scene1_spawn_origin[1] + 2.5f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, g_scene1_spawn_origin[2]);
    }

    /* L483: rot.x += 0.15. */
    float rot_x = slot_get_f(i, SCENE1_RECORDS_A_OFF_ROT_X);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rot_x + 0.15f);

    /* L484: vel.x = 0. */
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, 0.0f);

    /* L485: vel.y = 0.2 - (float)age — gets overwritten at L489-91,
     * but the intermediate write is observable (Ghidra preserves it).
     * Skip the intermediate; only the final vel.y survives. */
    int age = slot_age(i);

    /* L486-L488: angle = (age * π/4) / 32 = age * π / 128. */
    float angle    = ((float)age * 0.7853982f) / 32.0f;
    float cos_term = cosf(angle);

    /* L489-491: vel.y = (param2 - 127) * 0.002 + cos_term * 0.2
     *                   - (float)age * 0.005. */
    float vel_y = ((float)(param2 - 0x7f)) * 0.002f
                + cos_term * 0.2f
                - (float)age * 0.005f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vel_y);

    /* L492: vel.z = 0. */
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, 0.0f);

    slot_age_inc(i);
    age = slot_age(i);  /* refresh after increment */

    /* L494-L497: table-B-referenced kill gate.  If PARAM2 == -1 use
     * scene_alive; otherwise read g_scene1_records_b[param2 * 0x49]
     * (the "slot active" field at field-0). */
    int gate;
    if (param2 == -1) {
        gate = g_scene1_scene_alive;
    } else if (param2 >= 0 && param2 < SCENE1_RECORDS_B_COUNT) {
        gate = g_scene1_records_b[param2 * SCENE1_RECORDS_B_STRIDE];
    } else {
        /* Out-of-range PARAM2.  Engine doesn't bounds-check; we
         * conservatively treat OOB as "still alive" to avoid a crash
         * read.  Real engine path would segfault — match retail's
         * lack of bounds-checking only if a Frida test shows the
         * crash is observable; otherwise the safe path is fine. */
        gate = g_scene1_scene_alive;
    }
    if (gate == 0) {
        slot_kill(i);
    }

    /* L501: hard cap on lifetime at age == 0x20 ticks. */
    if (age == 0x20) {
        slot_kill(i);
    }
}

/* ─── handler: type 0x68 — pure pos += vel (no damp), age++, kill ───
 *
 * Engine decomp L124-L134.  Unusual — no damp multiplication.  Often
 * used for ballistic dust that just drifts and dies. */
static void handle_type_68(int i)
{
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz);
    slot_age_inc(i);
    if (slot_age(i) == 0x30) slot_kill(i);
}

/* ─── handler: types 0x96 / 0x97 — gravity-fall with rot bumps ──────
 *
 * Engine decomp L153-L170.  pos += vel; rot.x += 0.1; rot.y += 0.03;
 * rot.z += 0.01.  Then damp 0.995 on all axes, vel.y -= 0.03, kill at
 * 0x40.  Used for "puff" effects (smoke?) that slowly rotate while
 * settling.
 *
 * Order from decomp: pos += vel, rot += ..., vel *= damp,
 * vel.y -= grav, age++, kill check. */
static void handle_type_96_97(int i)
{
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_X, 0.1f);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.03f);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 0.01f);
    vx *= 0.995f;  vy *= 0.995f;  vz *= 0.995f;
    vy -= 0.03f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_age_inc(i);
    if (slot_age(i) == 0x40) slot_kill(i);
}

/* ─── handler: types 0x36 / 0x74 — gravity-fall, kill at PARAM1 ─────
 *
 * Engine decomp L338-L352.  damp 0.97 pre-grav, gravity -0.02.  Kill
 * threshold is the per-record PARAM1 field.
 *
 * Type 0x4e (L353-L367) is structurally identical. */
static void handle_type_36_74_4e(int i)
{
    int kill_at = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1);
    decay_drift_grav_pre(i, 0.97f, -0.02f, kill_at);
}

/* ─── handler: type 0x29 — gravity -0.002, damp 0.97, kill 0x28 ─────
 *
 * Engine decomp L429-L443.  Note the gravity write order: vel.y -=
 * 0.002 is between pos-add and the damp, so it qualifies as pre-damp
 * gravity. */
static void handle_type_29(int i)
{
    decay_drift_grav_pre(i, 0.97f, -0.002f, 0x28);
}

/* ─── handler: types 0x1f / 100 — gravity -0.001, damp 0.97, kill 0x20 */
static void handle_type_1f_100(int i)
{
    decay_drift_grav_pre(i, 0.97f, -0.001f, 0x20);
}

/* ─── handler: type 0x45 — anchor-attached vel.y bias ───────────────
 *
 * Engine decomp L594-L608.  Unusual: pre-damp gravity reads
 * BASE_Z (slot+11) and adds it to vel.y BEFORE the damp.  damp 0.97,
 * kill 0x80.
 *
 * Verbatim: `vel.y = BASE_Z + vel.y;` then the standard damp.  Used
 * for an effect where Z-position acts as a vertical force coupling
 * (perhaps water-buoyancy or wave-drag). */
static void handle_type_45(int i)
{
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz);
    float base_z = slot_get_f(i, SCENE1_RECORDS_A_OFF_BASE_Z);
    vy += base_z;
    vx *= 0.97f;  vy *= 0.97f;  vz *= 0.97f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_age_inc(i);
    if (slot_age(i) == 0x80) slot_kill(i);
}

/* ─── handler: type 0x60 — pure age, capped at 400, kill at 0x960 ───
 *
 * Engine decomp L69-L76.  Age stops advancing at 400 but slot only
 * dies at 0x960.  Suggests "max bloom alpha at 400, hold until kill". */
static void handle_type_60(int i)
{
    if (slot_age(i) < 400) slot_age_inc(i);
    if (slot_age(i) == 0x960) slot_kill(i);
}

/* ─── handler: type 0x5d — pure age, kill at PARAM1 + 0x3e ──────────
 *
 * Engine decomp L218-L222 — combined into one if/else: age++ then
 * kill if age == PARAM1 + 0x3e. */
static void handle_type_5d(int i)
{
    slot_age_inc(i);
    int kill_at = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) + 0x3e;
    if (slot_age(i) == kill_at) slot_kill(i);
}

/* ─── handler: types 0x4b / 0x55 / 0x4c — field-decay variants ──────
 *
 * Engine decomp L505-L527.  rot.y += vel.x; age++; kill at K (or at
 * PARAM2 + 0x28 for 0x4b). */
static void handle_type_4b(int i)
{
    float v = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, v);
    slot_age_inc(i);
    int kill_at = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) + 0x28;
    if (slot_age(i) == kill_at) slot_kill(i);
}

/* ─── handler: types 0x33 / 0x4d — field-decay to ROT_X, kill 0x30 ──
 *
 * Engine decomp L529-L536.  rot.x += vel.x; age++; kill 0x30. */
static void handle_type_33_4d(int i)
{
    field_decay_x(i, SCENE1_RECORDS_A_OFF_ROT_X, 1.0f, 0x30);
}

/* ─── handler: type 0x51 — rot.x += vel.x * 0.5, kill 0x20 ──────────
 *
 * Engine decomp L537-L544.  Half-rate field decay. */
static void handle_type_51(int i)
{
    field_decay_x(i, SCENE1_RECORDS_A_OFF_ROT_X, 0.5f, 0x20);
}

/* ─── handler: type 0x57 — rot.x += vel.x * 0.5, kill 0xa0 ──────────
 *
 * Engine decomp L545-L552.  Same shape as 0x51, slower kill. */
static void handle_type_57(int i)
{
    field_decay_x(i, SCENE1_RECORDS_A_OFF_ROT_X, 0.5f, 0xa0);
}

/* ─── handler: type 0x3e — rot.x += vel.x, kill 0x30 ────────────────
 *
 * Engine decomp L553-L560.  Same as 0x33/0x4d but distinct match. */
static void handle_type_3e(int i)
{
    field_decay_x(i, SCENE1_RECORDS_A_OFF_ROT_X, 1.0f, 0x30);
}

/* ─── handler: type 0x32 — rot.x += 0.2, kill 0x40 ──────────────────
 *
 * Engine decomp L587-L593.  Constant rotation rate; no velocity link. */
static void handle_type_32(int i)
{
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_X, 0.2f);
    slot_age_inc(i);
    if (slot_age(i) == 0x40) slot_kill(i);
}

/* ─── handler: type 0x11 — vel.x += vel.y, kill 0x30 ────────────────
 *
 * Engine decomp L897-L904.  Funky "shear" feedback — vel.x grows from
 * vel.y.  Unique shape. */
static void handle_type_11(int i)
{
    slot_add_f(i, SCENE1_RECORDS_A_OFF_VEL_X,
               slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y));
    slot_age_inc(i);
    if (slot_age(i) == 0x30) slot_kill(i);
}

/* ─── handler: type 0x71 — pos += 2*vel, damp 0.96040004, age += 2 ──
 *
 * Engine decomp L958-L972.  Double-rate position step (twin add of
 * vel), damp 0.96040004 (= 0.98^2 — squared damp matches the double
 * time-step semantics).  age += 2 per call; kill when age > 0x7f. */
static void handle_type_71(int i)
{
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, 2.0f * vx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, 2.0f * vy);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, 2.0f * vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * 0.96040004f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * 0.96040004f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * 0.96040004f);
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) += 2;
    if (slot_age(i) > 0x7f) slot_kill(i);
}

/* ─── handler: types 0x10 / 0x91 — scaled drift + PARAM1 gravity ────
 *
 * Engine decomp L1078-L1094.  pos += vel*scale; vel.y -= PARAM1*0.003
 * (gravity strength is the int PARAM1 field); damp 0.92; kill 0x18.
 * PARAM1 sign convention: positive PARAM1 means downward gravity. */
static void handle_type_10_91(int i)
{
    float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
    int param1 = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1);
    vy -= (float)param1 * 0.003f;
    vx *= 0.92f;  vy *= 0.92f;  vz *= 0.92f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_age_inc(i);
    if (slot_age(i) == 0x18) slot_kill(i);
}

/* ─── handler: types 0x15 / 0x16 — scaled drift + rot decay split ───
 *
 * Engine decomp L1044-L1077.  pos += vel*scale; vel.y -= 0.01; damp x
 * 0.95, damp y 0.92, damp z 0.95.  Then rot bumps:
 *   - 0x15: rot.x -= 0.03; rot.y -= 0.01; rot.z -= 0.03; kill at 0x7c
 *   - 0x16: rot.x -= 0.05; rot.y -= 0.03; rot.z -= 0.05; kill at 0xe0 */
static void handle_type_15_16(int i, int type)
{
    float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
    vy -= 0.01f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * 0.95f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * 0.92f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * 0.95f);

    float rot_dx, rot_dy, rot_dz;
    int kill_age;
    if (type == 0x15) {
        rot_dx = -0.03f;  rot_dy = -0.01f;  rot_dz = -0.03f;  kill_age = 0x7c;
    } else {
        rot_dx = -0.05f;  rot_dy = -0.03f;  rot_dz = -0.05f;  kill_age = 0xe0;
    }
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rot_dx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, rot_dy);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, rot_dz);
    slot_age_inc(i);
    if (slot_age(i) == kill_age) slot_kill(i);
}

/* ─── handler: types 0x3f / 0x56 — scaled drift + rot decay (shared) ─
 *
 * Engine decomp L1028-L1042.  pos += vel*scale; rot bumps same as
 * 0x16 (rot.x -= 0.05; rot.y -= 0.03; rot.z -= 0.05); kill 0x4d8. */
static void handle_type_3f_56(int i)
{
    float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_X, -0.05f);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, -0.03f);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, -0.05f);
    slot_age_inc(i);
    if (slot_age(i) == 0x4d8) slot_kill(i);
}

/* ─── handler: type 0x4f — scaled drift gated by age + PARAM2 ───────
 *
 * Engine decomp L994-L1010.  pos += vel*scale ONLY if age < PARAM2-2;
 * rot.z -= 0.005 inside the same gate.  age++ + kill at 0x8c always.
 * The gate is on the AGE BEFORE the increment. */
static void handle_type_4f(int i)
{
    int param2 = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2);
    if (slot_age(i) < param2 - 2) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, -0.005f);
    }
    slot_age_inc(i);
    if (slot_age(i) == 0x8c) slot_kill(i);
}

/* ─── handler: type 0x58 — scaled drift gated by age < PARAM2 ───────
 *
 * Engine decomp L1011-L1027.  pos += vel*scale if age < PARAM2; rot.y
 * += 0.02 in same gate.  Kill at 0x4d8. */
static void handle_type_58(int i)
{
    int param2 = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2);
    if (slot_age(i) < param2) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.02f);
    }
    slot_age_inc(i);
    if (slot_age(i) == 0x4d8) slot_kill(i);
}

/* ─── handler: types 5, 0x5c, 0x6f, 10, 0xb, 0xc — gated scaled-drift ─
 *
 * Engine decomp L807-L829.  pos += vel*scale gated by age > 0; damp
 * 0.95 always.  For types != 0x5c and != 0x6f, also rot.z += 0.1.
 * Kill at age == 0x18.
 *
 * "Gated by age > 0" means age=0 step does the damp and age++ but not
 * the position add. */
static void handle_type_group_drift_5_5c(int i, int type)
{
    if (slot_age(i) > 0) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * 0.95f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * 0.95f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * 0.95f);
        if (type != 0x5c && type != 0x6f) {
            slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 0.1f);
        }
    }
    slot_age_inc(i);
    if (slot_age(i) == 0x18) slot_kill(i);
}

/* ─── handler: types 0xe, 0x2b, 0x1b, 0x3b, 0x76 — gated scaled-drift ─
 *
 * Engine decomp L852-L872.  Same gate as 5/0x5c, damp 0.95, kill 0x20,
 * NO rot bump. */
static void handle_type_group_drift_e_2b(int i)
{
    if (slot_age(i) > 0) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * 0.95f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * 0.95f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * 0.95f);
    }
    slot_age_inc(i);
    if (slot_age(i) == 0x20) slot_kill(i);
}

/* ─── handler: type 0x59 — EXPANDING scaled drift, gated by age > 0 ──
 *
 * Engine decomp L873-L891.  Same as the above but damp 1.05 (negative
 * damp — particles accelerate outward).  No rot.  Kill 0x20. */
static void handle_type_59(int i)
{
    if (slot_age(i) > 0) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * 1.05f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * 1.05f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * 1.05f);
    }
    slot_age_inc(i);
    if (slot_age(i) == 0x20) slot_kill(i);
}

/* ─── handler: type 0xf — scaled drift, gated by age >= 0, damp 0.9 ──
 *
 * Engine decomp L939-L957.  Kill at 0x10. */
static void handle_type_f(int i)
{
    if (slot_age(i) >= 0) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * 0.9f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * 0.9f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * 0.9f);
    }
    slot_age_inc(i);
    if (slot_age(i) == 0x10) slot_kill(i);
}

/* ─── handler: type 0x67 — scaled drift with z-only soft damp + rot ──
 *
 * Engine decomp L830-L851.  pos += vel*scale (gated age > 0); damps
 * are split: x,y = 0.95, z = 0.99; vel.y += 0.01 (anti-gravity? bias).
 * rot.z += vel.z * 0.1 (rotation feedback from z-velocity).
 * Kill at 0x80. */
static void handle_type_67(int i)
{
    if (slot_age(i) > 0) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        vx *= 0.95f;  vy *= 0.95f;  vz *= 0.99f;
        vy += 0.01f;
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, vz * 0.1f);
    }
    slot_age_inc(i);
    if (slot_age(i) == 0x80) slot_kill(i);
}

/* ─── handler: large group [0x25..0x90] — scaled drift + gravity +
 *               rot-accumulator
 *
 * Engine decomp L905-L938.  ~30 types share one body:
 *   0x25, 0x26, 0x27, 0x28, 0x37, 0x38, 0x39, 0x3a, 0x46, 0x47, 0x48,
 *   0x49, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83,
 *   0x84, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
 *   0x90.
 *
 * Gate: age >= 0.  pos += vel*scale, vel.y -= 0.02, damp 0.95 (all).
 * Unconditionally (regardless of gate):  rot.z += rot.y; rot.y *= 0.97.
 * age++ + kill at 0x20.
 *
 * Used for the wide-followup walker's particle-spam effects (sparkles
 * around an action). */
static int type_in_huge_group(int t)
{
    static const unsigned char list[] = {
        0x25, 0x26, 0x27, 0x28, 0x37, 0x38, 0x39, 0x3a, 0x46, 0x47,
        0x48, 0x49, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81,
        0x82, 0x83, 0x84, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c,
        0x8d, 0x8e, 0x8f, 0x90
    };
    for (unsigned k = 0; k < sizeof list; k++) {
        if ((int)list[k] == t) return 1;
    }
    return 0;
}

static void handle_type_huge_group(int i)
{
    if (slot_age(i) >= 0) {
        float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
        slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
        vy -= 0.02f;
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx * 0.95f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy * 0.95f);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz * 0.95f);
    }
    /* rot accumulator runs unconditionally — outside the age gate. */
    float rot_y = slot_get_f(i, SCENE1_RECORDS_A_OFF_ROT_Y);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, rot_y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, rot_y * 0.97f);
    slot_age_inc(i);
    if (slot_age(i) == 0x20) slot_kill(i);
}

/* ─── handler: type 0x92 — color-cycle billboard tick (C8h.3 trig) ──
 *
 * Engine decomp L171-L189.  Sin-perturb vel.x each tick + standard
 * position step + 0.015707964 (~π/200) rotation triad bump.  Kill at
 * 0x100.
 *
 * Used by the wide-followup walker's Pass F — the integrator
 * companion of the C8g.2 MVP that already paints the static record.
 * Once this lands and 0x92 records get spawned, Pass F will animate
 * (rotate + drift) instead of holding still.
 */
static void handle_type_92(int i)
{
    /* L172: phase = (PARAM1 + age) * 0.05 — the per-particle phase
     * stored in PARAM1 keeps spawn-time entropy across rebirth. */
    int param1 = *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1);
    int age    = slot_age(i);
    float phase = (float)(param1 + age) * 0.05f;
    float perturb = sinf(phase) * 0.001f;

    /* L175-176: vel.x += sin(phase) * 0.001. */
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X) + perturb;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);

    /* L177-181: pos += vel (using the freshly-perturbed vel.x). */
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y,
               slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y));
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z,
               slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z));

    /* L182-184: rot += π/200 on all three axes (slow spin). */
    const float spin = 0.015707964f;
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_X, spin);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, spin);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, spin);

    slot_age_inc(i);
    if (slot_age(i) == 0x100) slot_kill(i);
}

/* ─── handler: type 0x18 — random-sin (vel.y drives via rot.y phase) ─
 *
 * Engine decomp L974-L993.  vel.y is REPLACED each tick by
 * sin(rot.y) * 0.03; then standard scaled-drift; rot bumps decay
 * (rot.x -= 0.05, rot.y -= 0.03, rot.z -= 0.05).  Kill at 0x4d8.
 *
 * Engine quirk: line 976 has the argless `FUN_00503a44()` —
 * unambiguous here because line 975 stores `local_8 = rot.y` and
 * immediately calls sin; the FPU TOS is rot.y.  Confidence: HIGH.
 */
static void handle_type_18(int i)
{
    float ry = slot_get_f(i, SCENE1_RECORDS_A_OFF_ROT_Y);
    float new_vy = sinf(ry) * 0.03f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, new_vy);

    float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = new_vy;
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);

    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);

    /* Rot decay — matches type 0x3f/0x56's per-tick rate. */
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_X, -0.05f);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, -0.03f);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, -0.05f);

    slot_age_inc(i);
    if (slot_age(i) == 0x4d8) slot_kill(i);
}

/* ─── handler: type 0x34 — orbiting projectile + chained-spawn 0x35 ──
 *
 * Engine decomp L368-L391.  Age-gated; rotates a translation vector
 * `(0, 0, distance)` by RotX(vel.y) × RotY(vel.z), reads the
 * translation row (M[12..14]) as a displacement, adds player anchor.
 * vel.z += 0.05 each tick (the orbit rate increases).
 *
 * Kill at age == 0x18; on kill, chain-spawn type 0x35 at the final
 * position.  Goes through `scene1_spawn` (stub today, real C8i later).
 */
static void handle_type_34(int i)
{
    int age = slot_age(i);
    if (age >= 0) {
        float base_x = g_scene1_player_pos[0];
        float base_y = g_scene1_player_pos[1] + 2.0f;
        float base_z = g_scene1_player_pos[2];

        float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);

        float dist = (float)(0x18 - age) * vx;

        /* Engine chain (L374-L380):  M = T(0,0,0); M = M * RotX(vy);
         * M = M * RotY(vz); M = M * T(0,0,dist).  Under our
         * mat4_mul(out, a, b) = a*b convention with row-vector
         * semantics, this is the standard "rotate then translate"
         * chain. */
        float M[16], scratch[16];
        mat4_translation(M, 0.0f, 0.0f, 0.0f);
        mat4_rotation_x(scratch, vy);
        mat4_mul(M, M, scratch);
        mat4_rotation_y(scratch, vz);
        mat4_mul(M, M, scratch);
        mat4_translation(scratch, 0.0f, 0.0f, dist);
        mat4_mul(M, M, scratch);

        /* M[12..14] is the translation row — the rotated displacement
         * vector. */
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, M[12] + base_x);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, M[13] + base_y);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, M[14] + base_z);

        /* L384: vel.z += 0.05.  The orbit rate accelerates over the
         * particle's lifetime. */
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz + 0.05f);
    }

    slot_age_inc(i);
    if (slot_age(i) == 0x18) {
        float px = slot_get_f(i, SCENE1_RECORDS_A_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_A_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_A_OFF_POS_Z);
        /* Engine call L388: FUN_00447f4f(0, pos.x, pos.y, pos.z, 0x35) —
         * trailing scale + param7 defaults as in type 0x20's chain. */
        scene1_spawn(0, px, py, pz, 0x35, 1.0f, 0);
        slot_kill(i);
    }
}

/* ─── handler: type 0x35 — orbital body anchored to player ──────────
 *
 * Engine decomp L392-L415.  Age-gated; computes vel from rotated
 * unit-Z vector by RotX(rot.y) × RotY(rot.z) × T(0,0,1).  Position
 * snaps to player anchor each tick.  Kill at age == 0x30.
 *
 * Pair with 0x34 — this is the secondary orbit that spawns when
 * 0x34's lifetime ends.
 */
static void handle_type_35(int i)
{
    int age = slot_age(i);
    if (age >= 0) {
        float base_x = g_scene1_player_pos[0];
        float base_y = g_scene1_player_pos[1] + 2.0f;
        float base_z = g_scene1_player_pos[2];

        float ry = slot_get_f(i, SCENE1_RECORDS_A_OFF_ROT_Y);
        float rz = slot_get_f(i, SCENE1_RECORDS_A_OFF_ROT_Z);

        float M[16], scratch[16];
        mat4_translation(M, 0.0f, 0.0f, 0.0f);
        mat4_rotation_x(scratch, ry);
        mat4_mul(M, M, scratch);
        mat4_rotation_y(scratch, rz);
        mat4_mul(M, M, scratch);
        mat4_translation(scratch, 0.0f, 0.0f, 1.0f);
        mat4_mul(M, M, scratch);

        /* vel = M[12..14] — the rotated unit-Z vector. */
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, M[12]);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, M[13]);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, M[14]);

        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, base_x);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, base_y);
        slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, base_z);
    }

    slot_age_inc(i);
    if (slot_age(i) == 0x30) slot_kill(i);
}

/* ─── handler: types 4, 0x70, 0x1c — scaled drift + rot.z drip ───────
 *
 * Engine decomp L1205-L1224.  pos += vel*scale; rot.z += 0.1; age++.
 * Kill at 0x20 for type 0x70, else 0x10. */
static void handle_type_group_4_70_1c(int i, int type)
{
    float scale = slot_get_f(i, SCENE1_RECORDS_A_OFF_SCALE);
    float vx = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * scale);
    slot_add_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 0.1f);
    slot_age_inc(i);
    int kill_at = (type == 0x70) ? 0x20 : 0x10;
    if (slot_age(i) == kill_at) slot_kill(i);
}

/* ─── outer loop ─────────────────────────────────────────────────────
 *
 * Engine FUN_0040fb3a L49-L1247.  Walks 0..0x1000-1 and runs every
 * type handler in sequence against the per-slot TYPE field.  Empty
 * slots short-circuit on the first compare (TYPE == -1 matches no
 * handler).
 *
 * Handlers re-read TYPE each time because a previous handler in the
 * chain may have killed the slot (TYPE = -1) — the next handler's
 * compare then fails naturally, which matches engine semantics for
 * chained kills (e.g. type 0x20 setting TYPE = -1 within the same
 * iteration must prevent any subsequent handler from also matching).
 */
void scene1_particles_tick(void)
{
    particles_per_frame_open();

    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        int type = slot_type(i);

        /* Camera-orbit attract — 4 types share one body. */
        if (type == 6 || type == 7 || type == 8 || type == 9) {
            handle_type_6_to_9(i);
        }

        /* Player-snap (re-read; type 6..9 may have killed). */
        type = slot_type(i);
        if (type == 0x20) {
            handle_type_20(i);
        }

        /* Cone-spread (re-read; type 0x20 may have killed). */
        type = slot_type(i);
        if (type == 0x21) {
            handle_type_21(i);
        }

        /* ─── C8h.2: cookie-cutter decay-drift / pure-age / field-decay
         * handlers.  Each block re-reads TYPE because a prior handler
         * (in the slot's same iteration) may have killed it.
         *
         * The if-chain order MATCHES the engine's decomp order from
         * FUN_0040fb3a — never re-order, because the order interacts
         * with intra-tick kills.  TYPE == -1 short-circuits every
         * compare below. */

        type = slot_type(i);
        if (type == 0x43)        { decay_drift_uniform(i, 0.97f, 0x18); }

        type = slot_type(i);
        if (type == 0x60)        { handle_type_60(i); }

        type = slot_type(i);
        if (type == 0x53)        { decay_drift_uniform(i, 0.97f, 0x18); }

        /* (type 99 / 0x78 land in C8h.4 — they read player_pos or NPC
         * table.) */

        type = slot_type(i);
        if (type == 0x68)        { handle_type_68(i); }

        /* Group [1, 0x5e, 2, 3, 0x52, 0x40, 0x65, 0x66, 0x73, 0x77]:
         * decay-drift-uniform, damp 0.97, kill 0x18.  Engine L136-L151. */
        type = slot_type(i);
        if (type == 1 || type == 0x5e || type == 2 || type == 3 ||
            type == 0x52 || type == 0x40 || type == 0x65 || type == 0x66 ||
            type == 0x73 || type == 0x77) {
            decay_drift_uniform(i, 0.97f, 0x18);
        }

        type = slot_type(i);
        if (type == 0x96 || type == 0x97) { handle_type_96_97(i); }

        type = slot_type(i);
        if (type == 0x92)        { handle_type_92(i); }

        type = slot_type(i);
        if (type == 0x69)        { decay_drift_uniform(i, 0.98f, 0x80); }

        type = slot_type(i);
        if (type == 0x79)        { decay_drift_uniform(i, 0.998f, 0x131); }

        type = slot_type(i);
        if (type == 0x5d)        { handle_type_5d(i); }

        /* type 0x4a — matrix + NPC table 0xf8 stride.  Deferred to
         * C8h.4 (needs people-table-0xf8 stub). */

        /* type 0x98 — C8h.4 (anchor read). */

        /* types 0x75, 0x93 — C8h.4. */

        type = slot_type(i);
        if (type == 0x36 || type == 0x74) { handle_type_36_74_4e(i); }

        type = slot_type(i);
        if (type == 0x4e)        { handle_type_36_74_4e(i); }

        type = slot_type(i);
        if (type == 0x34)        { handle_type_34(i); }

        type = slot_type(i);
        if (type == 0x35)        { handle_type_35(i); }

        /* type 0x2c — C8h.4 (uses anchor). */

        type = slot_type(i);
        if (type == 0x29)        { handle_type_29(i); }

        /* types 0x41/0x61/0x72/0x62 — C8h.4 (anchor snap). */

        /* type 0x4b — pure field-decay variant. */
        type = slot_type(i);
        if (type == 0x4b)        { handle_type_4b(i); }

        type = slot_type(i);
        if (type == 0x55)        { field_decay_x(i, SCENE1_RECORDS_A_OFF_ROT_Y, 1.0f, 0x90); }

        type = slot_type(i);
        if (type == 0x4c)        { field_decay_x(i, SCENE1_RECORDS_A_OFF_ROT_Y, 1.0f, 0x28); }

        type = slot_type(i);
        if (type == 0x33 || type == 0x4d) { handle_type_33_4d(i); }

        type = slot_type(i);
        if (type == 0x51)        { handle_type_51(i); }

        type = slot_type(i);
        if (type == 0x57)        { handle_type_57(i); }

        type = slot_type(i);
        if (type == 0x3e)        { handle_type_3e(i); }

        /* type 0x3d — has trig.  C8h.3 (random-sin family). */

        type = slot_type(i);
        if (type == 0x32)        { handle_type_32(i); }

        type = slot_type(i);
        if (type == 0x45)        { handle_type_45(i); }

        /* type 0x6e — chained mesh-emit + anchor (C8h.4). */

        /* type 0x6d, 0x6c — anchor-related (C8h.4). */

        type = slot_type(i);
        if (type == 0x1f || type == 100) { handle_type_1f_100(i); }

        /* types 0x23, 0x22, 0x3c, 0x5a, 0x2d — anchor (C8h.4). */

        type = slot_type(i);
        if (type == 0x24)        { age_only_kill_at(i, 0x100); }

        type = slot_type(i);
        if (type == 0x2a)        { age_only_kill_at(i, 8); }

        /* types 0x12, 0x13, 0x14 — anchor-gated (C8h.4). */

        type = slot_type(i);
        if (type == 0x54)        { age_only_kill_at(i, 0x78); }

        type = slot_type(i);
        if (type == 5 || type == 0x5c || type == 0x6f || type == 10 ||
            type == 0xb || type == 0xc) {
            handle_type_group_drift_5_5c(i, type);
        }

        type = slot_type(i);
        if (type == 0x67)        { handle_type_67(i); }

        type = slot_type(i);
        if (type == 0xe || type == 0x2b || type == 0x1b || type == 0x3b ||
            type == 0x76) {
            handle_type_group_drift_e_2b(i);
        }

        type = slot_type(i);
        if (type == 0x59)        { handle_type_59(i); }

        type = slot_type(i);
        if (type == 0x50)        { age_only_kill_at(i, 300); }

        type = slot_type(i);
        if (type == 0x11)        { handle_type_11(i); }

        type = slot_type(i);
        if (type_in_huge_group(type)) { handle_type_huge_group(i); }

        type = slot_type(i);
        if (type == 0xf)         { handle_type_f(i); }

        type = slot_type(i);
        if (type == 0x71)        { handle_type_71(i); }

        type = slot_type(i);
        if (type == 0x18)        { handle_type_18(i); }

        type = slot_type(i);
        if (type == 0x4f)        { handle_type_4f(i); }

        type = slot_type(i);
        if (type == 0x58)        { handle_type_58(i); }

        type = slot_type(i);
        if (type == 0x3f || type == 0x56) { handle_type_3f_56(i); }

        type = slot_type(i);
        if (type == 0x15 || type == 0x16) { handle_type_15_16(i, type); }

        type = slot_type(i);
        if (type == 0x10 || type == 0x91) { handle_type_10_91(i); }

        type = slot_type(i);
        if (type == 4 || type == 0x70 || type == 0x1c) {
            handle_type_group_4_70_1c(i, type);
        }

        /* type 0x44, 0x5f — pure-age (engine L1143-1147, L1199-1203). */
        type = slot_type(i);
        if (type == 0x44)        { age_only_kill_at(i, 0x28); }

        type = slot_type(i);
        if (type == 0x5f)        { age_only_kill_at(i, 0xd); }

        type = slot_type(i);
        if (type == 0x42)        { scaled_drift_uniform(i, 1.0f, 0x10); }
        /* NB: type 0x42 in decomp L1148-1160 has no damp multiplier
         * inside the body — pos += vel*scale; age++; kill 0x10.  Using
         * scaled_drift_uniform(damp=1.0) preserves that.  Same for 0x19,
         * 0x2e, 0x1e below. */

        type = slot_type(i);
        if (type == 0x19)        { scaled_drift_uniform(i, 1.0f, 0x10); }

        type = slot_type(i);
        if (type == 0x2e)        { scaled_drift_uniform(i, 1.0f, 0x38); }

        type = slot_type(i);
        if (type == 0x1e)        { scaled_drift_uniform(i, 1.0f, 0x10); }

        /* type 0x1d — anchor (C8h.4 — reads table-B record). */

        /* TODO C8h.3-.4: matrix transforms (0x4a, 0x34, 0x35), trig
         * (0x18, 0x3d, 0x92), and anchor-snap (0x1a/0x12-14/0x78/0x75/
         * 0x93/many others). */
    }
}
