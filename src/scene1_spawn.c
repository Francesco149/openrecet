/*
 * scene1_spawn.c — see scene1_spawn.h for the chip writeup.
 *
 * Port of FUN_00447f4f @ 0x447f4f (1449-line Ghidra decomp).  C8i.1
 * landed the outer slot-scan + common preamble + 3 anchor types
 * (0x60, 0x20, 0x66).  C8i.2 added the radial-burst family.  C8i.3c
 * adds 13 local_8-azimuth + chain-pair handlers (engine lines 528-735):
 *
 *   - Type 0x34 (24) — chain spawner: vel.x/y/z assignments are
 *     overwritten mid-body (the first batch of sin/cos*scale*mag writes
 *     is dead).  Final vel.x = mag*scale; vel.y/z = u*2π (rotation
 *     seeds repurposed as integrator scratch); pos = (x,y,z) - vel*24
 *     (anchor-back 24×); AGE = -i.  Pairs with 0x35 chain at kill time.
 *   - Type 0x35 (1)  — chain target: rot.x=0, rot.y/z = u*2π;
 *     pos = (x,y,z) - vel*24, with vel from the preamble (= 0), so
 *     pos = (x,y,z) exact.  AGE = -i (= 0 since first spawn).
 *   - Type 0x2c (32) — small radial mag=0.2*(u+1.2), pos = vel*20 +
 *     param, AGE = -i (negative stagger).
 *   - Type 0x29 (14) — direct vel = trig*(u+0.2)*0.6, vy = (u-0.2)*0.8
 *     (no scale factor!), no pos/age write.
 *   - Type 0x32 (2)  — string at π/2 offset: rot.x=i*π+π/2, rot.y=0,
 *     rot.z=π/2; AGE=0; PARAM2 = u & 0xff.
 *   - Types 0x4c, 0x55 (1) — string at π/4 offset, alternating vel.x
 *     sign per particle, slot.scale *= (1 - i*0.2).
 *   - Type 0x4b (3)  — same shape as 0x4c/0x55 with count-driven
 *     vel.x = (i+1)*0.1 and PARAM2 = i*2.
 *   - Types 0x33, 0x4d, 0x51 (param_7) — string w/ rot.x overwritten
 *     to u*2π (random); pos.y = (u-0.5)*scale*4 + y.
 *   - Type 0x57 (1)  — like 0x33 family but rot.z = ±π/2 based on
 *     param_7 sign; PARAM2 = param_7 (via LAB_00448f57).
 *   - Type 0x3e (4)  — like 0x33 family but with rot.z = u*0.2 + π/2
 *     jitter (no rot.x overwrite); 4 particles total.
 *
 * C8i.3b covers 9 mixed-shape multi-particle radials (engine lines 289-527):
 *
 *   - Type 0x53 (1)  — world-radial xz via trig*scale*8(u+2.2); vy =
 *     (u+1.5)*scale*3 (positive bias); xz vel = 0.
 *   - Type 0x4a (8)  — pos = exact (no nudge); rot.{x,y,z} = u*2π;
 *     PARAM1 = param_7; AGE = i*-4 (4-tick stagger).
 *   - Type 0x43 (24) — xz vel = 0; vy = (u+0.5)*scale*3; xz pos in
 *     world frame via trig × 2(u+2.2)*scale; pos.y = param_3 - scale*8.
 *   - Type 0x97 (64) — full spherical: elevation = u*π/2, azimuth =
 *     u*2π, vy biased by sin(elev)*mag*(u+1.5); scale *= (u+0.5)/2.
 *   - Type 0x96 (64) — same as 0x97 except vy bias is (u+1.0) and adds
 *     camera-angle xz bend (reads g_scene1_spawn_camera_counter_948,
 *     stand-in for engine's *(int*)(slot_hint + 0x948)); scale *= (u+0.5).
 *   - Type 0x40 (8)  — circular ring: azimuth = local_8 * 2π/8; vy =
 *     u*scale*1.5 (positive); shares LAB_0044ac8d with group A.
 *   - Types 0x36, 0x74 (param_7) — small radial mag=(u+0.5); slot scale
 *     re-randomized to (u+1.0)*param_6; PARAM1 = (u & 0xf) + 0x10.
 *   - Type 0x4e (3)  — narrowed xz vel = trig*scale*mag*0.1; pos in
 *     wider world-radial frame via trig × scale × (u+1.5)*3.
 *
 * C8i.3a covers 6 world-anchored radial variants (engine lines 143-264):
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

static inline float slot_read_f_inline(int i, int off)
{
    int32_t v = g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
    float f;
    __builtin_memcpy(&f, &v, sizeof f);
    return f;
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

/* Camera-counter stand-in for type 0x96's bend term.  Engine reads
 * `*(int *)(slot_hint + 0x948)` directly (slot_hint is overloaded — for
 * 0x96 callers, it's a pointer into the scene-state struct).  The
 * sim-side caller of scene1_spawn(0x96, ...) is unported; until its
 * stack layout is known we keep this as a writeable global that defaults
 * to 0 (sin(0)=0, cos(0)=1, so the bend term degenerates to a constant
 * vel.z+=0.2 push when unset).  Logged for raw-asm verification when the
 * caller ports.  Tests set it to confirm the trig dependency. */
int g_scene1_spawn_camera_counter_948;

/* PARAM1 = u%100+20; AGE = 0.  Tail of many radial-burst handlers
 * (LAB_00448643 / inline).  Single helper saves a few lines per handler. */
static void set_param1_random_life_age_zero(int i)
{
    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;
}

/* ─── per-type init: 0x53 — world-radial xz + positive-bias vy (L289-305) ─
 *
 *   mag    = (u1 + 2.2) * 8.0                      ; mag has no scale factor
 *   angle  = u2 * 2π
 *   vel.y  = (u3 + 1.5) * scale * 3.0              ; positive bias
 *   vel.x  = 0
 *   vel.z  = 0
 *   pos.y  = vel.y * 3.0 + y                        ; lift upward by vy*3
 *   pos.x  = sin(angle) * scale * mag + x          ; world-radial xz
 *   pos.z  = cos(angle) * scale * mag + z
 *   PARAM1 = u%100+20; AGE = 0  (via LAB_00448643)
 *
 * Returns after 1 particle. */
static void init_type_53(int i, float x, float y, float z, float scale)
{
    float u1    = rng_next_unit();
    float mag   = (u1 + 2.2f) * 8.0f;
    float angle = rng_next_unit() * TWO_PI_F;
    float vy    = (rng_next_unit() + 1.5f) * scale * 3.0f;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, sinf(angle) * scale * mag + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 3.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, cosf(angle) * scale * mag + z);

    set_param1_random_life_age_zero(i);
}

/* ─── per-type init: 0x4a — 8-spawn matrix-init w/ PARAM1=param7 (L307-333) ─
 *
 *   mag    = u1 + 1.2
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * scale * mag
 *   vel.y  = (u3 - 0.5) * scale * 3
 *   vel.z  = cos(angle) * scale * mag
 *   rot.x  = u4 * 2π                                ; rotation seeds
 *   rot.y  = u5 * 2π
 *   rot.z  = u6 * 2π
 *   pos    = (x, y, z)                              ; exact, no nudge
 *   PARAM1 = param_7                                 ; spawn-arg threaded
 *   AGE    = count_index * -4                       ; 4-tick stagger
 *
 * Commits 8 particles per call (LAB_0044acd2 → bVar11 = local_8 == 7).
 * Pairs with the integrator's handle_type_4a (chain spawned from 0x18
 * kills in C8h.4d). */
static void init_type_4a(int i, int count_index, float x, float y, float z,
                         float scale, int param7)
{
    float u1    = rng_next_unit();
    float mag   = u1 + 1.2f;
    float angle = rng_next_unit() * TWO_PI_F;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, sinf(angle) * scale * mag);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y,
               (rng_next_unit() - 0.5f) * scale * 3.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, cosf(angle) * scale * mag);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, rng_next_unit() * TWO_PI_F);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, z);

    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = param7;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = count_index * -4;
}

/* ─── per-type init: 0x43 — 24-spawn ring-from-below (L335-359) ───────
 *
 *   fVar1  = 2 * (u1 + 2.2) * scale                ; mag absorbs scale
 *   angle  = u2 * 2π                               ; ALSO stored as rot.y
 *   vel.x  = 0
 *   vel.y  = (u3 + 0.5) * scale * 3                ; mild positive bias
 *   vel.z  = 0
 *   rot.x  = 0
 *   rot.y  = u2 * 2π                               ; angle reused
 *   rot.z  = 0
 *   pos.x  = sin(angle) * fVar1 + x                ; (scale already in fVar1)
 *   pos.y  = y - scale * 8.0                       ; anchor 8 below
 *   pos.z  = cos(angle) * fVar1 + z
 *   PARAM1 = u%100+20; AGE = 0
 *
 * Commits 24 particles per call (LAB_00448d0b → bVar11 = local_8 == 0x17). */
static void init_type_43(int i, float x, float y, float z, float scale)
{
    float u1     = rng_next_unit();
    float fVar1  = 2.0f * (u1 + 2.2f) * scale;
    float u2     = rng_next_unit();
    float angle  = u2 * TWO_PI_F;
    float vy     = (rng_next_unit() + 0.5f) * scale * 3.0f;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, 0.0f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, angle);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 0.0f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, sinf(angle) * fVar1 + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y - scale * 8.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, cosf(angle) * fVar1 + z);

    set_param1_random_life_age_zero(i);
}

/* ─── per-type init: 0x97 — 64-spawn spherical (L361-394) ─────────────
 *
 *   mag       = (u1 + 0.5) * 1.5
 *   elev      = u2 * π/2                           ; elevation angle 0..π/2
 *   xy_radius = cos(elev) * mag
 *   azimuth   = u3 * 2π
 *   vel.x     = sin(azimuth) * scale * xy_radius
 *   vel.y     = (u4 + 1.5) * scale * sin(elev) * mag   ; upward (+1.5 bias)
 *   vel.z     = cos(azimuth) * scale * xy_radius
 *   rot.x/y/z = u5/u6/u7 * 2π
 *   scale_mul = (u8 + 0.5) / 2.0                   ; in [0.25, 0.75]
 *   slot.scale *= scale_mul
 *   PARAM1 = u%100+20; AGE = 0
 *
 * Commits 64 particles per call (local_8 == 0x3f → LAB_00448a67). */
static void init_type_97(int i, float x, float y, float z, float scale)
{
    float u1        = rng_next_unit();
    float mag       = (u1 + 0.5f) * 1.5f;
    float u2        = rng_next_unit();
    float elev      = u2 * 1.5707964f;
    float sin_elev  = sinf(elev);
    float cos_elev  = cosf(elev);
    float xy_radius = cos_elev * mag;

    float azimuth   = rng_next_unit() * TWO_PI_F;
    float sin_az    = sinf(azimuth);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, sin_az * scale * xy_radius);
    float u4 = rng_next_unit();
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y,
               (u4 + 1.5f) * scale * sin_elev * mag);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, cosf(azimuth) * scale * xy_radius);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, rng_next_unit() * TWO_PI_F);

    /* Engine LAB_00448a67: scale *= fVar12, where fVar12 = (u8+0.5)/2 for 0x97
     * and u8+0.5 for 0x96.  Re-read current scale because preamble set it. */
    float scale_mul = (rng_next_unit() + 0.5f) / 2.0f;
    float cur_scale = scale;  /* preamble wrote `scale` (= param_6) */
    slot_set_f(i, SCENE1_RECORDS_A_OFF_SCALE, scale_mul * cur_scale);

    set_param1_random_life_age_zero(i);

    (void)x; (void)y; (void)z;  /* 0x97 ignores xyz — preamble keeps pos = param */
}

/* ─── per-type init: 0x96 — 64-spawn spherical + camera-bend (L396-430) ─
 *
 * Like 0x97, but:
 *   - vy bias is (u4 + 1.0) instead of (u4 + 1.5)
 *   - vel.x += sin(camera) * 0.2;  vel.z += cos(camera) * 0.2
 *     where camera = g_scene1_spawn_camera_counter_948 * 2π / 8
 *   - pos = vel * 3 + (x, y, z)     (0x97 leaves pos = param)
 *   - scale_mul = u8 + 0.5          (0x97 uses /2)
 *
 * The camera term is the engine's `*(int *)(slot_hint + 0x948) * 2π/8`.
 * We swap in a stand-in global until the sim-side caller (which sets
 * slot_hint to a real pointer for 0x96) ports.  See top-of-file note. */
static void init_type_96(int i, float x, float y, float z, float scale)
{
    float u1        = rng_next_unit();
    float mag       = (u1 + 0.5f) * 1.5f;
    float u2        = rng_next_unit();
    float elev      = u2 * 1.5707964f;
    float sin_elev  = sinf(elev);
    float cos_elev  = cosf(elev);
    float xy_radius = cos_elev * mag;

    float azimuth   = rng_next_unit() * TWO_PI_F;
    float vx        = sinf(azimuth) * scale * xy_radius;
    float u4        = rng_next_unit();
    float vy        = (u4 + 1.0f) * scale * sin_elev * mag;
    float vz        = cosf(azimuth) * scale * xy_radius;

    float camera    = (float)g_scene1_spawn_camera_counter_948 * TWO_PI_F / 8.0f;
    vx += sinf(camera) * 0.2f;
    vz += cosf(camera) * 0.2f;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 3.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 3.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 3.0f + z);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, rng_next_unit() * TWO_PI_F);

    float scale_mul = rng_next_unit() + 0.5f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_SCALE, scale_mul * scale);

    set_param1_random_life_age_zero(i);
}

/* ─── per-type init: 0x40 — 8-spawn circular ring (L432-444) ──────────
 *
 *   mag    = u1 + 1.2
 *   angle  = (count_index * 2π) / 8                ; equal-spaced ring
 *   vel.x  = sin(angle) * scale * mag
 *   vel.y  = u2 * scale * 1.5                       ; positive bias (u2 >= 0)
 *   vel.z  = cos(angle) * scale * mag
 *   pos    = vel * 3 + (x,y,z); PARAM1 = u%100+20; AGE = 0  (LAB_0044ac8d)
 *
 * Commits 8 particles per call. */
static void init_type_40(int i, int count_index, float x, float y, float z,
                         float scale)
{
    float u1    = rng_next_unit();
    float mag   = u1 + 1.2f;
    float angle = ((float)count_index * TWO_PI_F) / 8.0f;

    float vx = sinf(angle) * scale * mag;
    float vy = rng_next_unit() * scale * 1.5f;
    float vz = cosf(angle) * scale * mag;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 3.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 3.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 3.0f + z);

    set_param1_random_life_age_zero(i);
}

/* ─── per-type init: 0x36, 0x74 — small radial w/ re-scaled SCALE (L474-497) ─
 *
 *   mag       = u1 + 0.5                           ; (smaller than group A)
 *   angle     = u2 * 2π
 *   vel.x     = sin(angle) * scale * mag
 *   vel.y     = (u3 - 0.5) * scale * 3
 *   vel.z     = cos(angle) * scale * mag
 *   pos       = (x, y, z)                          ; exact
 *   PARAM1    = (u & 0xf) + 0x10                   ; life cap 16..31
 *   AGE       = 0
 *   slot.scale = (u4 + 1.0) * scale                ; re-randomize 1..2× original
 *
 * Commits param_7 particles per call (LAB_0044aa47 → local_8+1 == param_7). */
static void init_type_36_74(int i, float x, float y, float z, float scale)
{
    float u1    = rng_next_unit();
    float mag   = u1 + 0.5f;
    float angle = rng_next_unit() * TWO_PI_F;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, sinf(angle) * scale * mag);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y,
               (rng_next_unit() - 0.5f) * scale * 3.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, cosf(angle) * scale * mag);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, z);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r & 0xfu) + 0x10;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_SCALE,
               (rng_next_unit() + 1.0f) * scale);
}

/* ─── per-type init: 0x4e — 3-spawn narrowed vel + wide pos (L499-527) ─
 *
 *   fVar1  = (u1 + 1.5) * 3.0                      ; mag (scale absorbed at vel)
 *   angle  = u2 * 2π                               ; reused for vel + pos
 *   vel.x  = sin(angle) * scale * fVar1 * 0.1      ; narrowed
 *   vel.y  = (u3 + 0.5) * scale * 1.5
 *   vel.z  = cos(angle) * scale * fVar1 * 0.1
 *   pos.x  = sin(angle) * scale * fVar1 + x        ; wider (no 0.1 narrowing)
 *   pos.y  = y                                     ; exact
 *   pos.z  = cos(angle) * scale * fVar1 + z
 *   PARAM1 = (u & 0xf) + 0x10
 *   AGE    = 0
 *   slot.scale = (u4 + 1.0) * scale
 *
 * Commits 3 particles per call (local_8 == 2 → return). */
static void init_type_4e(int i, float x, float y, float z, float scale)
{
    float u1    = rng_next_unit();
    float fVar1 = (u1 + 1.5f) * 3.0f;
    float angle = rng_next_unit() * TWO_PI_F;
    float sa    = sinf(angle);
    float ca    = cosf(angle);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, sa * scale * fVar1 * 0.1f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y,
               (rng_next_unit() + 0.5f) * scale * 1.5f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, ca * scale * fVar1 * 0.1f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, sa * scale * fVar1 + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, ca * scale * fVar1 + z);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r & 0xfu) + 0x10;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_SCALE,
               (rng_next_unit() + 1.0f) * scale);
}

/* ─── per-type init: 0x34 — 24-spawn chain spawner (engine L528-551) ──
 *
 *   The engine first computes a sin/cos*scale*mag triple into vel, then
 *   IMMEDIATELY overwrites those values:
 *     vel.x ← (u1+1.2) * scale          ; was sin(u2*2π)*scale*(u1+1.2)
 *     vel.y ← u4 * 2π                    ; was (u3-0.5)*scale*3
 *     vel.z ← u5 * 2π                    ; was cos(u2*2π)*scale*(u1+1.2)
 *   pos = (x,y,z) - vel*24
 *   AGE = -count_index
 *
 * The first three RNG draws (u1/u2/u3) are kept because the dead writes
 * still consume them (RNG state is observable from sibling spawns).  The
 * argless cosf() is dead-coded and gets dropped.
 *
 * Pairs with the integrator's handle_type_34 (C8h.3), which chain-spawns
 * a 0x35 follow-up at age=0x18.  24 particles per call (LAB_00448d0b). */
static void init_type_34(int i, int count_index, float x, float y, float z,
                         float scale)
{
    /* Engine dead-write batch: consume u1, u2, u3 for RNG-state parity
     * (downstream type-0x35 chain spawn will see the same PRNG sequence
     * the engine does).  The cos call is dead-coded since vel.z gets
     * overwritten before any other consumer reads it. */
    float u1 = rng_next_unit();
    float u2 = rng_next_unit();
    (void)sinf(u2 * TWO_PI_F);  /* dead store to vel.x */
    (void)rng_next_unit();      /* u3 — dead store to vel.y */
    /* Engine also calls cosf() here with no arg.  No observable side
     * effect since the result is dead.  Skip. */

    float mag = u1 + 1.2f;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, mag * scale);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, rng_next_unit() * TWO_PI_F);

    float vx = slot_read_f_inline(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f_inline(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f_inline(i, SCENE1_RECORDS_A_OFF_VEL_Z);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, x - vx * 24.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y - vy * 24.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, z - vz * 24.0f);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = -count_index;
}

/* ─── per-type init: 0x35 — 1-spawn chain target (engine L553-563) ─────
 *
 *   rot.x = 0
 *   rot.y = u * 2π
 *   rot.z = u * 2π
 *   pos = (x,y,z) - vel * 24                       ; vel is preamble (= 0)
 *   AGE = -count_index                              ; = 0 since 1-spawn
 *
 * Since the preamble zeroed vel before this handler runs, the pos write
 * degenerates to pos = (x,y,z).  Spawned by 0x34's kill chain. */
static void init_type_35(int i, int count_index, float x, float y, float z)
{
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, rng_next_unit() * TWO_PI_F);

    /* vel is 0 from preamble; pos = (x,y,z) - 0*24 = (x,y,z) exact.
     * Write through anyway to match engine's literal store. */
    float vx = slot_read_f_inline(i, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f_inline(i, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f_inline(i, SCENE1_RECORDS_A_OFF_VEL_Z);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, x - vx * 24.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, y - vy * 24.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, z - vz * 24.0f);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = -count_index;
}

/* ─── per-type init: 0x2c — 32-spawn small radial w/ AGE stagger (L565-585) ─
 *
 *   fVar1  = 0.2 * (u1 + 1.2)
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * scale * fVar1
 *   vel.y  = (u3 - 0.5) * scale * 0.5              ; smaller mult than 0x69
 *   vel.z  = cos(angle) * scale * fVar1
 *   pos    = vel * 20 + (x,y,z)                    ; wide forward nudge
 *   PARAM1 = u%100+20
 *   AGE    = -count_index                          ; negative stagger
 *
 * Commits 32 particles per call (LAB_0044aafa → bVar11 = local_8 == 0x1f). */
static void init_type_2c(int i, int count_index, float x, float y, float z,
                         float scale)
{
    float u1    = rng_next_unit();
    float fVar1 = 0.2f * (u1 + 1.2f);
    float angle = rng_next_unit() * TWO_PI_F;

    float vx = sinf(angle) * scale * fVar1;
    float vy = (rng_next_unit() - 0.5f) * scale * 0.5f;
    float vz = cosf(angle) * scale * fVar1;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, vz);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_X, vx * 20.0f + x);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y, vy * 20.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Z, vz * 20.0f + z);

    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM1) = (int)(r % 100u) + 20;
    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = -count_index;
}

/* ─── per-type init: 0x29 — 14-spawn raw vel (engine L587-598) ────────
 *
 *   fVar1  = 0.6 * (u1 + 0.2)
 *   angle  = u2 * 2π
 *   vel.x  = sin(angle) * fVar1                    ; NO scale factor
 *   vel.y  = (u3 - 0.2) * 0.8                       ; NO scale, bias 0.2
 *   vel.z  = cos(angle) * fVar1
 *
 * No pos/age/PARAM1 write — preamble values stand (pos = param,
 * age = 0, PARAM1 = 0).  Commits 14 particles per call (local_8 == 0xd). */
static void init_type_29(int i)
{
    float u1    = rng_next_unit();
    float fVar1 = 0.6f * (u1 + 0.2f);
    float angle = rng_next_unit() * TWO_PI_F;

    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, sinf(angle) * fVar1);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Y, (rng_next_unit() - 0.2f) * 0.8f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_Z, cosf(angle) * fVar1);
}

/* ─── per-type init: 0x32 — 2-spawn string at π/2 offset (engine L628-635) ─
 *
 *   rot.x  = count_index * π + π/2
 *   rot.y  = 0
 *   rot.z  = π/2                                   ; engine literal 0x3fc90fdb
 *   AGE    = 0
 *   PARAM2 = u & 0xff
 *
 * pos/vel stay at preamble (pos = param, vel = 0).  2 particles
 * (LAB_0044abe9 → bVar11 = local_8 == 1). */
static void init_type_32(int i, int count_index)
{
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X,
               (float)count_index * 3.1415927f + 1.5707964f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.0f);
    /* 0x3fc90fdb = π/2 single-precision (1.5707964f). */
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 1.5707964f);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = 0;
    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = (int)(r & 0xffu);
}

/* ─── per-type init: 0x4c, 0x55 — 1-spawn string π/4 alt-sign (L637-656) ─
 *
 *   rot.x  = count_index * π + π/4
 *   rot.y  = 0
 *   rot.z  = π/2
 *   pos.y  = (u1 - 1.0) * scale * 0.5 + y
 *   fVar1  = 0.2 * (u2 + 0.2)
 *   vel.x  = fVar1; if (count_index & 1) vel.x = -fVar1   ; sign alt
 *   slot.scale *= (1 - count_index * 0.2)                ; decay
 *   AGE    = 0
 *   PARAM2 = u & 0xff
 *
 * 1 particle (LAB_0044a985). */
static void init_type_4c_55(int i, int count_index, float y, float scale)
{
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X,
               (float)count_index * 3.1415927f + 0.7853982f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 1.5707964f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y,
               (rng_next_unit() - 1.0f) * scale * 0.5f + y);

    float fVar1 = 0.2f * (rng_next_unit() + 0.2f);
    if (count_index & 1) fVar1 = -fVar1;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, fVar1);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_SCALE,
               (1.0f - (float)count_index * 0.2f) * scale);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = 0;
    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = (int)(r & 0xffu);
}

/* ─── per-type init: 0x4b — 3-spawn string π/4 count-driven (engine L658-676) ─
 *
 *   rot.x  = count_index * π + π/4
 *   rot.y  = 0
 *   rot.z  = π/2
 *   pos.y  = (u - 1.0) * scale * 0.5 + y
 *   fVar1  = (count_index + 1) * 0.1                ; count-driven, not random
 *   vel.x  = fVar1; if (count_index & 1) vel.x = -fVar1
 *   slot.scale *= (1 - count_index * 0.2)
 *   AGE    = 0
 *   PARAM2 = count_index * 2                        ; count-driven
 *
 * 3 particles (bVar11 = count_index + 1 == 3 → matches). */
static void init_type_4b(int i, int count_index, float y, float scale)
{
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X,
               (float)count_index * 3.1415927f + 0.7853982f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 1.5707964f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y,
               (rng_next_unit() - 1.0f) * scale * 0.5f + y);

    float fVar1 = (float)(count_index + 1) * 0.1f;
    if (count_index & 1) fVar1 = -fVar1;
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X, fVar1);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_SCALE,
               (1.0f - (float)count_index * 0.2f) * scale);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE)    = 0;
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = count_index * 2;
}

/* ─── per-type init: 0x33, 0x4d, 0x51 — param_7-count string (L678-695) ─
 *
 *   rot.x  = count_index * π + π/4                  ; initial
 *   rot.y  = 0
 *   rot.x  = u * 2π                                  ; OVERWRITTEN w/ random
 *   rot.z  = π/2
 *   pos.y  = (u - 0.5) * scale * 4 + y
 *   vel.x  = (u + 0.2) * 0.2
 *   AGE    = 0
 *   PARAM2 = u & 0xff
 *
 * Commits param_7 particles per call (LAB_0044a8ef → LAB_0044aa47).
 * The initial rot.x = i*π+π/4 store is dead — engine overwrites
 * immediately, but the RNG draw still happens between. */
static void init_type_33_4d_51(int i, float y, float scale)
{
    /* Initial rot.x assignment is dead-coded by the immediate
     * overwrite below; skip. */
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z, 1.5707964f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y,
               (rng_next_unit() - 0.5f) * scale * 4.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X,
               (rng_next_unit() + 0.2f) * 0.2f);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = 0;
    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = (int)(r & 0xffu);
}

/* ─── per-type init: 0x57 — 1-spawn string w/ param7 sign (L697-717) ───
 *
 *   rot.x  = count_index * π + π/4 (dead)
 *   rot.y  = 0
 *   rot.x  = u * 2π
 *   rot.z  = (param_7 == 0) ? π/2 : -π/2            ; sign on param_7
 *   pos.y  = (u - 0.5) * scale * 4 + y
 *   vel.x  = (u + 0.2) * 0.2
 *   AGE    = 0
 *   <PRNG consumed and discarded>
 *   PARAM2 = param_7                                ; via LAB_00448f57
 *
 * 1 particle (LAB_0044a985). */
static void init_type_57(int i, float y, float scale, int param7)
{
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X, rng_next_unit() * TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z,
               (param7 == 0) ? 1.5707964f : -1.5707964f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y,
               (rng_next_unit() - 0.5f) * scale * 4.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X,
               (rng_next_unit() + 0.2f) * 0.2f);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = 0;
    (void)rng_next15();   /* engine consumes and discards a u15 here */

    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = param7;
}

/* ─── per-type init: 0x3e — 4-spawn string w/ rot.z jitter (L719-734) ──
 *
 *   rot.x  = count_index * π + π/4                  ; (NO overwrite this time)
 *   rot.y  = 0
 *   rot.z  = u * 0.2 + π/2                          ; jitter around π/2
 *   pos.y  = (u - 0.5) * scale * 4 + y
 *   vel.x  = (u + 0.2) * 0.2
 *   AGE    = 0
 *   PARAM2 = u & 0xff
 *
 * 4 particles (bVar11 = count_index == 3). */
static void init_type_3e(int i, int count_index, float y, float scale)
{
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_X,
               (float)count_index * 3.1415927f + 0.7853982f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_ROT_Z,
               rng_next_unit() * 0.2f + 1.5707964f);

    slot_set_f(i, SCENE1_RECORDS_A_OFF_POS_Y,
               (rng_next_unit() - 0.5f) * scale * 4.0f + y);
    slot_set_f(i, SCENE1_RECORDS_A_OFF_VEL_X,
               (rng_next_unit() + 0.2f) * 0.2f);

    *slot_int(i, SCENE1_RECORDS_A_OFF_AGE) = 0;
    uint16_t r = rng_next15();
    *slot_int(i, SCENE1_RECORDS_A_OFF_PARAM2) = (int)(r & 0xffu);
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
 * 0x78 (1).  C8i.3b adds 9 mixed-shape multi-particle radials — 0x53
 * (1), 0x4a (8), 0x43 (24), 0x97/0x96 (64), 0x40 (8), 0x36/0x74
 * (param_7), 0x4e (3).  C8i.3c adds 13 local_8-azimuth + chain pair —
 * 0x34 (24), 0x35 (1), 0x2c (32), 0x29 (14), 0x32 (2), 0x4c/0x55 (1),
 * 0x4b (3), 0x33/0x4d/0x51 (param_7), 0x57 (1), 0x3e (4).  Remaining
 * counts (12 for the mega-group, remainder of param_7-driven, etc.)
 * land in C8i.3d / C8i.4-5.  See scene1-spawn.md for the full table. */
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
    case 0x53: return 1;
    case 0x4a: return 8;
    case 0x43: return 24;
    case 0x97: case 0x96: return 64;
    case 0x40: return 8;
    case 0x4e: return 3;
    case 0x34: return 24;
    case 0x35: return 1;
    case 0x2c: return 32;
    case 0x29: return 14;
    case 0x32: return 2;
    case 0x4c: case 0x55: return 1;
    case 0x4b: return 3;
    case 0x57: return 1;
    case 0x3e: return 4;
    /* 0x36 / 0x74 / 0x33 / 0x4d / 0x51 use param_7 — see
     * spawn_count_is_param7() + scene1_spawn(). */
    default:   return 0;   /* unimplemented — record trace only */
    }
}

/* Returns 1 if `type` uses param_7 as its loop count (LAB_0044aa47).
 * Spawn() reads this to decide whether `want` comes from
 * spawn_count_for_type() or from param_7 (clamped to >=1). */
static int spawn_count_is_param7(int type)
{
    return (type == 0x36) || (type == 0x74) ||
           (type == 0x33) || (type == 0x4d) || (type == 0x51);
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
    case 0x53: init_type_53(i, x, y, z, scale); break;
    case 0x4a: init_type_4a(i, count_index, x, y, z, scale, param7); break;
    case 0x43: init_type_43(i, x, y, z, scale); break;
    case 0x97: init_type_97(i, x, y, z, scale); break;
    case 0x96: init_type_96(i, x, y, z, scale); break;
    case 0x40: init_type_40(i, count_index, x, y, z, scale); break;
    case 0x36: case 0x74:
        init_type_36_74(i, x, y, z, scale);
        break;
    case 0x4e: init_type_4e(i, x, y, z, scale); break;
    case 0x34: init_type_34(i, count_index, x, y, z, scale); break;
    case 0x35: init_type_35(i, count_index, x, y, z); break;
    case 0x2c: init_type_2c(i, count_index, x, y, z, scale); break;
    case 0x29: init_type_29(i); break;
    case 0x32: init_type_32(i, count_index); break;
    case 0x4c: case 0x55:
        init_type_4c_55(i, count_index, y, scale);
        break;
    case 0x4b: init_type_4b(i, count_index, y, scale); break;
    case 0x33: case 0x4d: case 0x51:
        init_type_33_4d_51(i, y, scale);
        break;
    case 0x57: init_type_57(i, y, scale, param7); break;
    case 0x3e: init_type_3e(i, count_index, y, scale); break;
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
    if (want == 0 && spawn_count_is_param7(type)) {
        /* Engine LAB_0044aa47: bVar11 = local_8 + 1 == param_7.  param_7
         * <= 0 returns after the first spawn (0+1 == 0/-N triggers signed
         * wrap path).  Clamp to >= 1 to keep the loop bounded. */
        want = param7 > 0 ? param7 : 1;
    }
    if (want == 0) {
        /* Unimplemented type — do not commit a slot.  When C8i.3c..5
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
