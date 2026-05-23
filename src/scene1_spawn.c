/*
 * scene1_spawn.c — see scene1_spawn.h for the chip writeup.
 *
 * Port of FUN_00447f4f @ 0x447f4f (1449-line Ghidra decomp).  C8i.1
 * landed the outer slot-scan + common preamble + 3 anchor types
 * (0x60, 0x20, 0x66).  C8i.2 added the radial-burst family.  C8i.3a
 * adds 6 world-anchored radial variants (engine lines 143-264):
 *
 *   - Type 0x69 — 128 particles, group-A-like body but with mag =
 *     2*(u+0.2), signed vy = (u-0.5)*scale*3, plus AGE = -count/2
 *     stagger (shares LAB_004481fa with 0x79).
 *   - Type 0x68 — 1 particle, "down-shifted" body: mag = 0.2*(u+1.2),
 *     sin→vx, cos→vy (NOT vz), vz = 2*(u-0.5)*scale*mag, then pos =
 *     param - vel*48 (large anchor-back).
 *   - Types 0x73, 0x77 — 2 particles, vel.z=-1 fixed, then vel.x/.z
 *     overwritten with -sin/-cos using angle = param_7/65536.0,
 *     pos = param + (u-0.5)*scale*15 jitter (world cube spread).
 *   - Type 99  — 1 particle, mag = u+1.2, anchor-back pos = param -
 *     vel*40, BASE = vel*-40 (recovery target).
 *   - Type 0x78 — same body as 99 plus PARAM2 = param_7.
 *
 * C8i.2 covers:
 *
 *   - Group A (types 1, 2, 3, 0x52, 0x5e, 0x65) — 8 particles each,
 *     radial xz spread with per-type vy variants.
 *   - Type 0x92 — 1 particle, color-cycle billboard burst (sets rot
 *     seeds + PARAM1/PARAM2 random scratch).
 *   - Type 0x79 — 128 particles, narrow xz spread + AGE stagger
 *     (AGE = -count/2).
 *   - Type 0x5d — 45 particles, wider xz spread + AGE/PARAM1 stagger
 *     (AGE = PARAM1 = -count).
 *
 * Translation conventions:
 *
 *   - Per-slot field access uses SCENE1_RECORDS_A_OFF_* (slot base =
 *     DAT_069b2f80; TYPE at +12 dw; etc.) — same as the integrator.
 *
 *   - thunk_FUN_005041f6() → rng_next15() (15-bit PRNG int).
 *   - FUN_00471089()       → rng_next_unit() (float in [0,1)).
 *   - FUN_00503a44 / FUN_00503994 → sinf / cosf (FPU thunks).
 *
 *   - The engine's outer loop scans slot indices 0..4095 (`do { … }
 *     while(local_10 != 0x1000)`).  We mirror this exactly — the
 *     scanning order is observable via the resulting slot indices, which
 *     the integrator's later type-0x18 / type-0x4a handlers consume.
 *
 *   - The trace ring is recorded at the top of scene1_spawn() so the
 *     pre-C8i tests (which assert on the trace after type-0x21 calls)
 *     continue to pass while the per-type init body for 0x21 is unported.
 *
 * Note on argless `FUN_00503994()` cos calls (engine L82 / L120 / L266 /
 * etc.): Ghidra drops the FPU-stack arg.  The integrator's matching
 * pattern (handle_type_21, handle_type_34) treats these as cos(angle)
 * where `angle` was the most-recent expression pushed for the paired
 * sin call.  Each radial-burst handler below uses that interpretation,
 * pre-binding the angle to a local float and passing it explicitly to
 * both sinf() and cosf().  Logged for raw-asm verification in
 * openrecet_pending_human_checks.
 */

#include "scene1_spawn.h"

#include <math.h>

#include "rng.h"
#include "scene1_records.h"

#define TWO_PI_F 6.2831855f  /* matches engine literal at L55/86/100/etc. */

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

/* ─── per-type init: group A — radial burst (engine L47-L80) ──────────
 *
 * Types 1, 2, 3, 0x52, 0x5e, 0x65 — all share this body; each commits 8
 * particles per spawn call.  Pattern:
 *
 *   mag   = u1 + 1.2                              ; mag base (radial scale)
 *   if (type == 0x65) mag *= 0.5                  ; small-burst variant
 *   angle = u2 * 2π                                ; xz angle
 *   vel.x = sin(angle) * SCALE * mag
 *   if (type == 0x52)
 *       vel.y = u3 * SCALE * 1.5                  ; upward bias
 *   else
 *       vel.y = (u3 - 0.5) * SCALE * 3.0          ; signed bias
 *   vel.z = cos(angle) * SCALE * mag              ; engine's argless cosf
 *   pos   = vel * 3.0 + (x,y,z)                   ; nudge along velocity
 *   PARAM1 = rng_next15() % 100 + 20              ; life cap 20..119
 *   age   = 0
 *
 * RNG ordering matches engine: u1, u2, u3, then PARAM1 LCG step. */
static void init_type_group_a(int i, float x, float y, float z, float scale,
                              int type)
{
    float u1    = rng_next_unit();
    float mag   = u1 + 1.2f;
    if (type == 0x65) mag *= 0.5f;

    float angle = rng_next_unit() * TWO_PI_F;
    float vx    = sinf(angle) * scale * mag;

    float vy;
    if (type == 0x52) {
        vy = rng_next_unit() * scale * 1.5f;
    } else {
        vy = (rng_next_unit() - 0.5f) * scale * 3.0f;
    }
    float vz = cosf(angle) * scale * mag;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 3.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 3.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 3.0f + z);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    /* AGE already 0 from preamble (engine L77). */
}

/* ─── per-type init: 0x92 — color-cycle billboard burst (engine L82-L108) ─
 *
 *   fVar1  = (u1 + 1.2) * 0.02                    ; tiny mag base
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * SCALE * fVar1
 *   vel.y  = (u3 + 2.5) * SCALE * -0.2            ; always-negative bias
 *   vel.z  = cos(angle) * SCALE * fVar1
 *   pos    = vel * 3.0 + (x,y,z)
 *   rot.x  = u4 * 2π                              ; random rotation seeds
 *   rot.y  = u5 * 2π
 *   rot.z  = u6 * 2π
 *   PARAM1 = rng_next15() % 1000                  ; color/age scratch
 *   PARAM2 = rng_next15() % 100 + 100             ; 100..199
 *   age    = 0
 *
 * Returns after 1 particle (LAB_004480f8 → LAB_0044a985). */
static void init_type_92(int i, float x, float y, float z, float scale)
{
    float u1    = rng_next_unit();
    float fVar1 = (u1 + 1.2f) * 0.02f;
    float angle = rng_next_unit() * TWO_PI_F;

    float vx = sinf(angle) * scale * fVar1;
    float u3 = rng_next_unit();
    float vy = (u3 + 2.5f) * scale * -0.2f;
    float vz = cosf(angle) * scale * fVar1;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 3.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 3.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 3.0f + z);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, rng_next_unit() * TWO_PI_F);

    uint16_t r1 = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r1 % 1000u);
    uint16_t r2 = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = (int)(r2 % 100u) + 100;
    /* AGE already 0 from preamble (engine L110). */
}

/* ─── per-type init: 0x79 — swarm-128 with AGE stagger (engine L120-L141) ──
 *
 *   fVar1  = u1 + 0.2                              ; smaller mag base
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * SCALE * fVar1 * 0.5     ; narrowed xz spread
 *   vel.y  = (u3 - 0.5) * SCALE                   ; mild signed bias
 *   vel.z  = (cos(angle) * fVar1 - fVar1 * 3.0) * SCALE  ; (-cos-3) bias
 *   pos    = vel * 3.0 + (x,y,z)
 *   PARAM1 = rng_next15() % 100 + 20
 *   AGE    = (int)count_index / -2                ; stagger 0,0,-1,-1,...
 *
 * Commits 128 particles per call (local_8 == 0x7f → 128 total). */
static void init_type_79(int i, int count_index, float x, float y, float z,
                         float scale)
{
    float u1    = rng_next_unit();
    float fVar1 = u1 + 0.2f;
    float angle = rng_next_unit() * TWO_PI_F;

    float vx = sinf(angle) * scale * fVar1 * 0.5f;
    float vy = (rng_next_unit() - 0.5f) * scale;
    float vz = (cosf(angle) * fVar1 - fVar1 * 3.0f) * scale;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 3.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 3.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 3.0f + z);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    /* Engine: AGE = (int)local_8 / -2.  C99 division truncates toward
     * zero, so 0/2=0, 1/2=0, 2/2=1, ... and negation gives the desired
     * 0,0,-1,-1,-2,-2,... stagger. */
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = count_index / -2;
}

/* ─── per-type init: 0x5d — swarm-45 with AGE/PARAM1 stagger (L266-L287) ──
 *
 *   mag    = u1 + 1.2                             ; (captured once for sin+cos)
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * SCALE * mag
 *   vel.y  = (u3 - 0.5) * SCALE * 3.0             ; signed bias
 *   vel.z  = cos(angle) * SCALE * mag
 *   pos    = vel * 4.0 + (x,y,z)                  ; NOTE: 4.0, not 3.0
 *   AGE    = -count_index                         ; 0,-1,-2,...,-44
 *   PARAM1 = -count_index                         ; same as AGE
 *
 * Commits 45 particles per call (local_8 == 0x2c → 45 total).  No
 * PRNG-derived life cap (engine sets PARAM1 = -local_8 instead). */
static void init_type_5d(int i, int count_index, float x, float y, float z,
                         float scale)
{
    float u1    = rng_next_unit();
    float mag   = u1 + 1.2f;
    float angle = rng_next_unit() * TWO_PI_F;

    float vx = sinf(angle) * scale * mag;
    float vy = (rng_next_unit() - 0.5f) * scale * 3.0f;
    float vz = cosf(angle) * scale * mag;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 4.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 4.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 4.0f + z);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = -count_index;
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = -count_index;
}

/* ─── per-type init: 0x69 — 128-spawn group-A swarm (engine L143-156) ──
 *
 *   mag    = 2*(u1 + 0.2)                          ; wider mag than 0x79
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * SCALE * mag              ; full radial (no 0.5)
 *   vel.y  = (u3 - 0.5) * SCALE * 3.0              ; signed bias
 *   vel.z  = cos(angle) * SCALE * mag
 *   pos    = vel * 3.0 + (x,y,z)
 *   PARAM1 = rng_next15() % 100 + 20
 *   AGE    = (int)count_index / -2                 ; matches 0x79 stagger
 *
 * Commits 128 particles per call (shares LAB_004481fa with 0x79). */
static void init_type_69(int i, int count_index, float x, float y, float z,
                         float scale)
{
    float u1    = rng_next_unit();
    float mag   = 2.0f * (u1 + 0.2f);
    float angle = rng_next_unit() * TWO_PI_F;

    float vx = sinf(angle) * scale * mag;
    float vy = (rng_next_unit() - 0.5f) * scale * 3.0f;
    float vz = cosf(angle) * scale * mag;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 3.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 3.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 3.0f + z);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = count_index / -2;
}

/* ─── per-type init: 0x68 — single down-shifted radial (engine L158-175) ─
 *
 *   fVar1  = 0.2 * (u1 + 1.2)                      ; small mag
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * SCALE * fVar1
 *   vel.y  = cos(angle) * SCALE * fVar1            ; cos→VY, not VZ
 *   vel.z  = 2 * (u3 - 0.5) * SCALE * fVar1        ; signed scratter
 *   pos    = (x,y,z) - vel * 48.0                  ; large anchor-back
 *   PARAM1 = rng_next15() % 100 + 20
 *   AGE    = 0
 *
 * Note the engine's slot-write order swaps the sin/cos pairing (sin→vx,
 * cos→vy) instead of the more common sin→vx, cos→vz pattern of group A.
 * Returns after 1 particle (goto LAB_004480f8). */
static void init_type_68(int i, float x, float y, float z, float scale)
{
    float u1    = rng_next_unit();
    float fVar1 = 0.2f * (u1 + 1.2f);
    float angle = rng_next_unit() * TWO_PI_F;

    float vx = sinf(angle) * scale * fVar1;
    float vy = cosf(angle) * scale * fVar1;
    float vz = 2.0f * (rng_next_unit() - 0.5f) * scale * fVar1;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, x - vx * 48.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y - vy * 48.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, z - vz * 48.0f);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;
}

/* ─── per-type init: 0x73/0x77 — vel-down + param7-trig (engine L186-215) ─
 *
 *   vel    = (0, 0, -1.0)                          ; pre-write before overwrite
 *   fVar1  = 0.2 * (u1 + 1.2)
 *   angle  = (float)param_7 / 65536.0              ; fixed-point Q16 angle
 *   vel.x  = -sin(angle) * SCALE * fVar1
 *   vel.y  = 0   (re-written)
 *   vel.z  = -cos(angle) * SCALE * fVar1
 *   pos.x  = (u2 - 0.5) * SCALE * 15.0 + x         ; world cube jitter
 *   pos.y  = (u3 - 0.5) * SCALE * 15.0 + y
 *   pos.z  = (u4 - 0.5) * SCALE * 15.0 + z
 *   PARAM1 = rng_next15() % 100 + 20
 *   AGE    = 0
 *
 * Commits 2 particles per call (LAB_0044abe9 → bVar11 = local_8 == 1).
 * The vel=(0,0,-1) pre-write is overwritten — kept only because the
 * engine writes it before the conditional, and matches the slot byte
 * order for any cycle-accurate dumps. */
static void init_type_73_77(int i, float x, float y, float z, float scale,
                            int param7)
{
    /* Engine pre-writes vel=(0,0,-1); we go straight to the overwritten
     * values since they fully clobber the initial set. */
    float u1    = rng_next_unit();
    float fVar1 = 0.2f * (u1 + 1.2f);
    float angle = (float)param7 / 65536.0f;

    float vx = -sinf(angle) * scale * fVar1;
    float vz = -cosf(angle) * scale * fVar1;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X,
               (rng_next_unit() - 0.5f) * scale * 15.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y,
               (rng_next_unit() - 0.5f) * scale * 15.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z,
               (rng_next_unit() - 0.5f) * scale * 15.0f + z);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;
}

/* ─── per-type init: 99 — anchor-back recovery (engine L217-238) ──────
 *
 *   mag    = u1 + 1.2                              ; group-A mag
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * SCALE * mag
 *   vel.y  = (u3 - 0.5) * SCALE * 3.0              ; signed bias
 *   vel.z  = cos(angle) * SCALE * mag
 *   pos    = (x,y,z) - vel * 40.0                  ; anchor-back 40×
 *   base   = vel * -40.0                           ; same as displacement
 *   PARAM1 = rng_next15() % 100 + 20  (via LAB_00448643)
 *   AGE    = 0
 *
 * The BASE field (slot offsets 9/10/11) records the displacement so a
 * downstream tick handler can interpolate the particle back to the
 * spawn origin.  Returns after 1 particle. */
static void init_type_63(int i, float x, float y, float z, float scale)
{
    float u1    = rng_next_unit();
    float mag   = u1 + 1.2f;
    float angle = rng_next_unit() * TWO_PI_F;

    float vx = sinf(angle) * scale * mag;
    float vy = (rng_next_unit() - 0.5f) * scale * 3.0f;
    float vz = cosf(angle) * scale * mag;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, x - vx * 40.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y - vy * 40.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, z - vz * 40.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_BASE_X, vx * -40.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_BASE_Y, vy * -40.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_BASE_Z, vz * -40.0f);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;
}

/* ─── per-type init: 0x78 — anchor-back + PARAM2 = param_7 (L240-264) ──
 *
 * Identical to type 99 plus PARAM2 = param_7.  Returns after 1 particle. */
static void init_type_78(int i, float x, float y, float z, float scale,
                         int param7)
{
    init_type_63(i, x, y, z, scale);
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = param7;
}

/* ─── dispatch: returns how many slots to commit for `type`.
 *
 * C8i.1 anchors spawn 1.  C8i.2 adds the radial-burst family — group A
 * (8), 0x92 (1), 0x79 (128), 0x5d (45).  C8i.3a adds 6 world-anchored
 * radial variants — 0x69 (128), 0x68 (1), 0x73/0x77 (2), 99 (1),
 * 0x78 (1).  Remaining counts (12 for the mega-group, param_7-driven,
 * etc.) land in C8i.3b-5.  See scene1-spawn.md for the full table. */
static int spawn_count_for_type(int type)
{
    switch (type) {
    case 0x60: return 1;
    case 0x20: return 1;
    case 0x66: return 1;
    case 0x92: return 1;
    case 1: case 2: case 3:
    case 0x52: case 0x5e: case 0x65:
        return 8;
    case 0x79: return 128;
    case 0x5d: return 45;
    case 0x69: return 128;
    case 0x68: return 1;
    case 0x73: case 0x77: return 2;
    case 99:   return 1;
    case 0x78: return 1;
    default:   return 0;   /* unimplemented — record trace only */
    }
}

/* Per-type init dispatch.  Called once per committed slot.  count_index
 * is the engine's `local_8` (0-based particle index within this call).
 * param7 is the spawn API's 7th positional arg — used by 0x78 / 0x73 /
 * 0x77 (and later by 0x4a / 0x12 / 0x21 in C8i.3+). */
static void run_type_init(int type, int i, int count_index, float x, float y,
                          float z, float scale, int param7)
{
    switch (type) {
    case 0x60: init_type_60(i); break;
    case 0x20: init_type_20(i); break;
    case 0x66: init_type_66(i); break;
    case 1: case 2: case 3:
    case 0x52: case 0x5e: case 0x65:
        init_type_group_a(i, x, y, z, scale, type);
        break;
    case 0x92: init_type_92(i, x, y, z, scale); break;
    case 0x79: init_type_79(i, count_index, x, y, z, scale); break;
    case 0x5d: init_type_5d(i, count_index, x, y, z, scale); break;
    case 0x69: init_type_69(i, count_index, x, y, z, scale); break;
    case 0x68: init_type_68(i, x, y, z, scale); break;
    case 0x73: case 0x77:
        init_type_73_77(i, x, y, z, scale, param7);
        break;
    case 99:   init_type_63(i, x, y, z, scale); break;
    case 0x78: init_type_78(i, x, y, z, scale, param7); break;
    default: break;
    }
    (void)count_index;  /* anchor types don't need it */
    (void)param7;
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
        run_type_init(type, local_10, got, x, y, z, scale, param7);
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
