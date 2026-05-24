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
#include "scene1_records.h"
#include "scene1_records_c_tick.h"

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
