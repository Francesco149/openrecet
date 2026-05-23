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

        /* TODO C8h.2-.4: the remaining ~91 type handlers land here.
         * Engine semantics: each handler re-reads the type from
         * memory, so verbatim placement of new `if` blocks at the
         * same point in the per-slot loop is the safe convention. */
    }
}
