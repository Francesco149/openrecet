/*
 * scene1_wide_followup_helpers.c — D3D-free helpers for
 * scene1_wide_followup.c.
 *
 * Same split as scene1_shop_walker_helpers.c: the algebraic per-record
 * helpers (filter, scale, tile index, UV box, world matrix composition)
 * live here so host unit tests can link them without pulling in <d3d8.h>.
 * scene1_wide_followup.c itself stays `#ifdef _WIN32` for the actual
 * walker entry (SetTransform / SetTexture / DrawPrimitiveUP).
 *
 * Today this TU holds the Pass C helpers (C8f.pass-c).  Future per-pass
 * body ports (A/B/D/E) will add siblings.
 */

#include "scene1_wide_followup.h"

#include <string.h>

#include "math3d.h"
#include "scene1_records.h"           /* SCENE1_RECORDS_B_OFF_* slot offsets */
#include "scene1_records_c_tick.h"    /* SCENE1_RECORDS_C_OFF_* slot offsets */

/* ─── Pass C: per-record filter ────────────────────────────────────────
 *
 * Engine FUN_004161c7 L147-150:
 *
 *   fVar1 = *local_8;
 *   if ((fVar1 != -NAN) &&
 *       ((((fVar1 == 0.0 || (fVar1 == 1.4013e-45)) ||
 *           fVar1 == 2.8026e-45) || fVar1 == 4.2039e-45)))
 *
 * The denormal-float comparisons match cardinal-int bit patterns
 * {0, 1, 2, 3} stored in slot[TYPE].  The `-NAN` short-circuit is the
 * 0xFFFFFFFF sentinel.  Our allocator (scene1_records_c_spawn) writes
 * TYPE as int, so we compare as int directly. */
int wf_pass_c_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_C_OFF_TYPE];
    if (type == -1) return 0;
    return (type == 0 || type == 1 || type == 2 || type == 3);
}

/* ─── Pass C: per-record scale (read from EXTRA_AUX, not TYPE) ─────────
 *
 * Engine L151-165 reads `local_8[7]` (= slot offset TYPE+7 = EXTRA_AUX,
 * offset 17 dw) as cardinal float — the same denormal-int trick — and
 * dispatches three scale buckets:
 *
 *   slot[EXTRA_AUX] == 1  →  0.0096      (small)
 *   slot[EXTRA_AUX] == 2  →  0.028800001 (large)
 *   else                  →  0.0192      (default)
 *
 * EXTRA_AUX is allocator's param_9 / piVar4[2].  The wide-followup's
 * survey doc mislabeled this as a per-TYPE scale; the field is
 * orthogonal and survives across the type filter.  Verbatim from
 * engine literal at .rdata (raw 0x3c9d3f00 = 0.0192; 0x3c1d3f00 = 0.0096;
 * 0x3cebd5e3 = 0.028800001).  */
float wf_pass_c_per_record_scale(const int32_t *slot)
{
    int32_t aux = slot[SCENE1_RECORDS_C_OFF_EXTRA_AUX];
    if (aux == 1) return 0.0096f;
    if (aux == 2) return 0.028800001f;
    return 0.0192f;
}

/* ─── Pass C: tile index in the 8×4 atlas ──────────────────────────────
 *
 * Engine L174-184:
 *
 *   local_c = (float)(((int)local_8[1] / 3) % 7);     // base 0..6
 *   if (type == 1) local_c += 8;
 *   if (type == 2) local_c += 16;
 *   if (type == 3) local_c += 24;
 *
 * `local_8[1]` is slot[TYPE+1] = slot[AGE] (read as signed int — the
 * decomp's `(int)` cast pre-empts any FPU conversion).  bmp/magicjem.tga
 * is 512×256 with 64-px tiles → 8 columns × 4 rows = 32 tiles, type
 * banded by 8s.  Engine writes `% 7` (not `% 8`) so tile column 7 of
 * each row is unused — the 7th tile slot is the empty one.  */
int wf_pass_c_tile_index(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_C_OFF_TYPE];
    int32_t age  = slot[SCENE1_RECORDS_C_OFF_AGE];
    int base = (age / 3) % 7;
    int type_offset = 0;
    if (type == 1) type_offset = 8;
    if (type == 2) type_offset = 16;
    if (type == 3) type_offset = 24;
    return base + type_offset;
}

/* ─── Pass C: UV box for a tile in the 512×256 atlas ───────────────────
 *
 * Engine L185-194:
 *
 *   col = tile % 8
 *   row = tile / 8
 *   u0 = (col*64 + 0.5) / 512.0
 *   u1 = (col*64 + 63.5) / 512.0
 *   v0 = (row*64 + 0.5) / 256.0
 *   v1 = (row*64 + 63.0) / 256.0  (NB: 63.0, not 63.5 — engine asymmetry)
 *
 * The 0.5-px inset on both axes is the standard "half-texel correction"
 * to avoid sampling neighboring tiles in TRIANGLESTRIP linear-filter
 * mode.  The v1 = 63.0 (vs 63.5 elsewhere) is engine-verbatim; ported
 * as-is.  */
void wf_pass_c_uv_box(int tile,
                      float *out_u0, float *out_u1,
                      float *out_v0, float *out_v1)
{
    int col = tile % 8;
    int row = tile / 8;
    *out_u0 = ((float)col * 64.0f +  0.5f) / 512.0f;
    *out_u1 = ((float)col * 64.0f + 63.5f) / 512.0f;
    *out_v0 = ((float)row * 64.0f +  0.5f) / 256.0f;
    *out_v1 = ((float)row * 64.0f + 63.0f) / 256.0f;
}

/* ─── Pass C: world matrix composition ─────────────────────────────────
 *
 * Engine L157 + L171-173:
 *
 *   Translation(M, POS_X, POS_Y, POS_Z);
 *   Scaling(S, scale, scale, scale);
 *   M = S * M                       // Multiply(M, S, M) — left-mul
 *   M = DAT_0438cdf8 * M            // Multiply(M, DAT_0438cdf8, M)
 *
 * DAT_0438cdf8 is a 4×4 matrix populated by an unidentified writer
 * outside the Ghidra-visible source.  Pre-multiplied last (i.e.
 * applied first under D3D's row-vector convention M_total = S * T then
 * tilted by DAT_0438cdf8).  We model it as a module-static stand-in
 * via scene1_wide_followup_get_pass_c_pre_matrix(); default identity
 * → benign no-op equivalent to omitting the multiply.  Logged as a
 * pending-human-check item for HOUSE smoke validation.
 *
 * Same `Multiply(M, A, M)` left-multiply convention as C8h.4d's fix
 * (thunk_FUN_004a2a03 is D3DXMatrixMultiply(out, A, B) → out = A * B).  */
void wf_pass_c_compose_world(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_C_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_C_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_C_OFF_POS_Z];
    float scale = wf_pass_c_per_record_scale(slot);

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, scale, scale, scale);
    mat4_mul(out, scratch, out);

    mat4_mul(out, wf_pass_c_get_pre_matrix(), out);
}

/* ─── Pass C: pre-matrix stand-in (DAT_0438cdf8) ───────────────────────
 *
 * Module-static 4×4 matrix initialized to identity, mutable via
 * scene1_wide_followup_set_pass_c_pre_matrix.  The engine's writer is
 * unidentified today (no `=` write to &DAT_0438cdf8 in any visible
 * decompile, ~30 readers).  Identity is a benign stand-in: M_final =
 * I × S × T = S × T, equivalent to omitting the pre-matrix multiply.
 *
 * When the engine writer ports, callers should set this via the setter
 * instead of forking the helper.  Logged as a PHC item.  */
static float g_wf_pass_c_pre_matrix[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

void wf_pass_c_set_pre_matrix(const float m[16])
{
    if (!m) return;
    memcpy(g_wf_pass_c_pre_matrix, m, sizeof(g_wf_pass_c_pre_matrix));
}

const float *wf_pass_c_get_pre_matrix(void)
{
    return g_wf_pass_c_pre_matrix;
}

/* ═══ Pass A: katter.tga billboard walker (C8f.pass-a) ════════════════════
 *
 * Engine FUN_004161c7 L51-91.  Walks g_scene1_records_b (stride 0x49)
 * filtering on type ∈ {0x77, 0xa2} stored as cardinal-int in TYPE
 * (offset 0).  The engine's `local_8` pointer is biased to slot+AGE
 * (offset 38), so the per-field accesses pick up:
 *
 *   local_8[-0x26]  = slot[TYPE]      (type filter source)
 *   *local_8        = slot[AGE]       (ramp-in clamp source)
 *   local_8[0x1c]   = slot[LIFE_MULT] (scale source, 0x1c=28; 38+28=66)
 *   local_8[-0xf]   = slot[POS_X]     (-15; 38-15=23)
 *   local_8[-0xe]   = slot[POS_Y]     (24)
 *   local_8[-0xd]   = slot[POS_Z]     (25)
 *   local_8[-2]     = slot[ROT_X]     (36; engine line `3.1415927 - local_8[-2]`)  */

/* Pass A filter: type in cardinal-int set {0x77, 0xa2}.  Engine compares
 * `fVar1` (read as float, but interpretation is bit-cast int — denormal
 * float values 1.66755e-43 = 0x77, 2.2701e-43 = 0xa2).  Our allocator
 * stores TYPE as int directly, so plain integer compare suffices.  Also
 * gates out the 0.0 (= TYPE 0, the free-slot sentinel) — see engine's
 * leading `fVar1 != 0.0` short-circuit. */
int wf_pass_a_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    if (type == 0) return 0;
    return (type == 0x77 || type == 0xa2);
}

/* Pass A per-record scale.  Engine L60-65:
 *
 *   local_c  = local_8[0x1c] * 0.005;       // slot[LIFE_MULT] * 0.005
 *   local_14 = *local_8;                    // slot[AGE] read as float bits
 *   if ((int)local_14 < 5) {
 *     local_14 = (float)(int)local_14;
 *     local_c  = (local_14 * local_c) / 5.0;
 *   }
 *
 * The (int) casts pre-empt any FPU conversion — engine stores AGE as
 * a cardinal int, the float read + int re-cast is a Ghidra artifact of
 * type-punning the same slot.  Effect: scale ramps 0/5, 1/5, 2/5, 3/5,
 * 4/5, then full from frame 5 onward.  LIFE_MULT defaults to 1.0 in the
 * allocator preamble, so the default first-frame scale is 0.0 (i.e.
 * a brand-new particle at AGE=0 is invisible). */
float wf_pass_a_per_record_scale(const int32_t *slot)
{
    float life_mult = *(const float *)&slot[SCENE1_RECORDS_B_OFF_LIFE_MULT];
    int32_t age = slot[SCENE1_RECORDS_B_OFF_AGE];
    float scale = life_mult * 0.005f;
    if (age < 5) scale = ((float)age * scale) / 5.0f;
    return scale;
}

/* Pass A per-record world matrix.  Engine L66-72:
 *
 *   Translation(M, POS_X, POS_Y, POS_Z);
 *   Scaling(S, scale, scale, scale);
 *   M = S * M;                      // Multiply(M, S, M)   → S * T
 *   RotationY(RY, π/2);             // thunk 3537 = RotY (per math3d.h)
 *   M = RY * M;                     // Multiply(M, RY, M)  → RY * S * T
 *   RotationZ(RZ, π - slot[ROT_X]); // thunk 3670 = RotZ
 *   M = RZ * M;                     // Multiply(M, RZ, M)  → RZ * RY * S * T
 *
 * Note: header comment (and C8f.1 skeleton) called this "RotZ(π/2) ×
 * RotY(π - yaw)" — that interpretation has the thunk identities swapped.
 * Per math3d.h's canonical mapping (3537=Y, 3670=Z), the engine builds
 * RotY(π/2) first then RotZ(π - rotX), so the corrected final shape is
 * RotZ(π - rotX) × RotY(π/2) × S × T.  Same left-multiply convention as
 * Pass C's compose_world.  */
void wf_pass_a_compose_world(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float rot_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_ROT_X];
    float scale = wf_pass_a_per_record_scale(slot);

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, scale, scale, scale);
    mat4_mul(out, scratch, out);

    mat4_rotation_y(scratch, 1.5707964f /* 0x3fc90fdb = π/2 */);
    mat4_mul(out, scratch, out);

    mat4_rotation_z(scratch, 3.1415927f - rot_x);
    mat4_mul(out, scratch, out);
}
