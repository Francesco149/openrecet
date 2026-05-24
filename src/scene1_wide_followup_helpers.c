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

#include <math.h>
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

/* ═══ Pass B: kumonosu.tga billboard walker (C8f.pass-b) ════════════════════
 *
 * Engine FUN_004161c7 L93-127.  Walks g_scene1_records_b (stride 0x49)
 * filtering on type == 0x53 (cardinal-int in TYPE / offset 0).  Engine's
 * `local_8` pointer is biased to slot+POS_Z (offset 25 — 13 dwords ahead
 * of Pass A's bias to slot+AGE), so the per-field accesses pick up:
 *
 *   local_8[-0x19]  = slot[TYPE]       (type filter source — denormal float
 *                                       1.16308e-43 = bit-cast 0x53)
 *   local_8[-2]     = slot[POS_X]      (23; 25-2=23)
 *   local_8[-1]     = slot[POS_Y]      (24)
 *   local_8[0]      = slot[POS_Z]      (25)
 *   local_8[0x29]   = slot[LIFE_MULT]  (66; 25+41=66)
 *
 * Same table memory as Pass A (just a different base register bias).
 * Texture: bmp/kumonosu.tga via g_sysassets.kumonosu_tga.tex (engine slot
 * DAT_073d8620, loaded by sysassets_load_all at boot, 128×128).  UV inset
 * 1/256 (0.00390625 / 0.99609375) — half-texel correction on the 128-px
 * source.  Matrix: T × S × RotY(π/2) (no RotZ — Pass B omits Pass A's
 * per-record yaw chain; the billboard is fixed-orientation). */

/* Pass B filter: type == 0x53 only.  Engine's `fVar1 != 0.0 && fVar1 ==
 * 1.16308e-43` short-circuit: the leading 0.0 check skips free slots
 * (TYPE 0 sentinel); 0x53 satisfies both.  Our allocator writes TYPE as
 * int, so plain integer compare against 0x53 suffices (0x53 ≠ 0 so the
 * sentinel guard is implicit). */
int wf_pass_b_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    return (type == 0x53);
}

/* Pass B per-record scale.  Engine L98: `local_c = local_8[0x29]` — direct
 * read of slot[LIFE_MULT] as a float, no multiplier, no ramp-in clamp.
 * LIFE_MULT defaults to 1.0 in the allocator preamble, so the default
 * scale is 1.0 — different from Pass A which multiplies by 0.005 and
 * ramps over 5 frames.  Pass B's quad is therefore visible from frame 0
 * at full size whenever a 0x53 slot exists. */
float wf_pass_b_per_record_scale(const int32_t *slot)
{
    return *(const float *)&slot[SCENE1_RECORDS_B_OFF_LIFE_MULT];
}

/* Pass B per-record world matrix.  Engine L103-107:
 *
 *   Translation(M, POS_X, POS_Y, POS_Z);
 *   Scaling(S, scale, scale, scale);
 *   M = S * M;                      // Multiply(M, S, M)   → S * T
 *   RotationY(RY, π/2);             // thunk 3537 = RotY (per math3d.h)
 *   M = RY * M;                     // Multiply(M, RY, M)  → RY * S * T
 *
 * Pass A has an additional RotZ(π - rotX) tail-multiply; Pass B does not.
 * Same left-multiply convention via mat4_mul (= D3DXMatrixMultiply) as
 * Pass A and Pass C. */
void wf_pass_b_compose_world(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float scale = wf_pass_b_per_record_scale(slot);

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, scale, scale, scale);
    mat4_mul(out, scratch, out);

    mat4_rotation_y(scratch, 1.5707964f /* 0x3fc90fdb = π/2 */);
    mat4_mul(out, scratch, out);
}

/* ═══ Pass E spear group (C8f.pass-e-spear) ═══════════════════════════════
 *
 * Engine FUN_004161c7 L293-L350.  Walks g_scene1_records_b (stride 0x49 —
 * engine's DAT_069324b0 is just the slot-base alias; Pass A's DAT_06932548
 * was the slot+AGE bias, Pass B's DAT_06932514 was the slot+POS_Z bias —
 * all three name the same record memory).  Engine's `piVar11` is biased to
 * the slot base, so field accesses are at slot offsets directly:
 *
 *   piVar11[0]      = slot[TYPE]      (filter source)
 *   piVar11[0x17]   = slot[POS_X]     (= 23)
 *   piVar11[0x18]   = slot[POS_Y]     (= 24)
 *   piVar11[0x19]   = slot[POS_Z]     (= 25)
 *   piVar11[0x24]   = slot[ROT_X]     (= 36; engine line `π - ROT_X`)
 *   piVar11[0x26]   = slot[AGE]       (= 38; ramp-in + 0x72 row-AGE quirk)
 *   piVar11[0x42]   = slot[LIFE_MULT] (= 66; scale source)
 *
 * The spear group {0x71, 0x72, 0x75} is the simpler half of Pass E; the
 * fan group {0x73, 0x7e, 0x78, 0xa0, 0x7a} is deferred until the
 * FUN_00415f2e camera-billboard helper survey lands.  Both groups share
 * texture DAT_073cc940 = bmp/effect_shot.bmp (256×256) and vbuf
 * DAT_0064bf68 (the same g_wf_pass_abe_vbuf shared with Passes A/B).  */

/* Pass E spear filter: cardinal-int type ∈ {0x71, 0x72, 0x75}.  Engine
 * does `iVar10 = *piVar11; if (iVar10 != 0)` upfront, then the spear
 * dispatch.  Our caller does the per-slot iteration so this just answers
 * "is this slot one of the spear types?". */
int wf_pass_e_spear_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    return (type == 0x71 || type == 0x72 || type == 0x75);
}

/* Pass E spear per-record scale.  Engine L300-308:
 *
 *   local_8 = piVar11[0x42] * 0.005;        // slot[LIFE_MULT] * 0.005
 *   local_1c = piVar11[0x26];                // slot[AGE]
 *   if ((int)local_1c < 5) {
 *     local_1c = (float)(int)local_1c;
 *     local_8  = (local_1c * local_8) / 5.0; // ramp-in over 5 frames
 *   }
 *   if (iVar10 == 0x72) local_8 = local_8 * 0.8;
 *
 * Same AGE<5 ramp-in as Pass A; 0x72 takes an extra 0.8 narrowing factor
 * after the ramp (i.e. 0x72's full-size scale is 0.8 × LIFE_MULT × 0.005).
 * LIFE_MULT defaults to 1.0 in the allocator preamble → first-frame scale
 * is 0.0 (invisible particle), full size from AGE 5 onward. */
float wf_pass_e_spear_per_record_scale(const int32_t *slot)
{
    float life_mult = *(const float *)&slot[SCENE1_RECORDS_B_OFF_LIFE_MULT];
    int32_t age = slot[SCENE1_RECORDS_B_OFF_AGE];
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    float scale = life_mult * 0.005f;
    if (age < 5) scale = ((float)age * scale) / 5.0f;
    if (type == 0x72) scale *= 0.8f;
    return scale;
}

/* Pass E spear UV tile selection.  Engine L320-338:
 *
 *   col = 128.0;  row = 192.0;                       // default (0x71)
 *   if (type == 0x72) {
 *     col = 192.0;
 *     row = (AGE % 4 < 2) ? 128.0 : 192.0;           // 2-frame anim
 *   }
 *   if (type == 0x75) { col = 192.0; row =   0.0; }
 *
 * (Engine also has a `type == 0x77` arm setting col=192, row=64, but
 * 0x77 is filtered out at the spear gate above — dead code in the
 * cardinal-int interpretation; preserved in comments only.)
 *
 * Each tile is 64×64 in the 256×256 atlas.  See wf_pass_e_spear_uv_box
 * for the 0.5-texel inset. */
void wf_pass_e_spear_tile(const int32_t *slot, float *out_col, float *out_row)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    int32_t age  = slot[SCENE1_RECORDS_B_OFF_AGE];
    float col = 128.0f, row = 192.0f;  /* 0x71 default */
    if (type == 0x72) {
        col = 192.0f;
        row = ((age % 4) < 2) ? 128.0f : 192.0f;
    }
    if (type == 0x75) {
        col = 192.0f;
        row =   0.0f;
    }
    *out_col = col;
    *out_row = row;
}

/* Pass E spear UV box for a (col, row) origin in the 256×256 atlas.
 * Engine L339-346:
 *
 *   u0 = (col +  0.5) / 256.0    v0 = (row +  0.5) / 256.0
 *   u1 = (col + 63.5) / 256.0    v1 = (row + 63.5) / 256.0
 *
 * 0.5-texel inset on both axes (symmetric — Pass C's 63.0 v1 asymmetry
 * does NOT carry over to Pass E).  64-px tile in a 256-px atlas. */
void wf_pass_e_spear_uv_box(float col, float row,
                            float *out_u0, float *out_u1,
                            float *out_v0, float *out_v1)
{
    *out_u0 = (col +  0.5f) / 256.0f;
    *out_u1 = (col + 63.5f) / 256.0f;
    *out_v0 = (row +  0.5f) / 256.0f;
    *out_v1 = (row + 63.5f) / 256.0f;
}

/* Pass E spear world matrix.  Engine L309-314:
 *
 *   Translation(M, POS_X, POS_Y, POS_Z);                // local_5c = T
 *   Scaling(S, scale, scale, scale);                    // local_15c = S
 *   M = S * M;                                          // M = S*T
 *   M = DAT_0438cdf8 * M;                               // M = pre*S*T
 *   RotationZ(RZ, π - ROT_X);                           // local_19c = RotZ
 *   M = RZ * M;                                         // M = RZ*pre*S*T
 *
 * thunk_FUN_004a3670 is RotZ per math3d.h's canonical mapping (same
 * thunk Pass A's per-record yaw uses).  The DAT_0438cdf8 pre-matrix has
 * no visible writer in the decompile — shared with Pass C (PHC #16).
 * Reuses Pass C's pre-matrix storage via wf_pass_c_get_pre_matrix() so
 * the engine global is modeled as a single stand-in; when the writer
 * ports, both passes pick up the live value with no extra wiring.  */
void wf_pass_e_spear_compose_world(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float rot_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_ROT_X];
    float scale = wf_pass_e_spear_per_record_scale(slot);

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, scale, scale, scale);
    mat4_mul(out, scratch, out);

    mat4_mul(out, wf_pass_c_get_pre_matrix(), out);

    mat4_rotation_z(scratch, 3.1415927f - rot_x);
    mat4_mul(out, scratch, out);
}

/* ═══ Pass E fan group (C8f.pass-e-fan) ═══════════════════════════════════
 *
 * Engine FUN_004161c7 L352-L409.  Same g_scene1_records_b memory as the
 * spear group; cardinal-int type filter {0x73, 0x7e, 0x78, 0xa0, 0x7a}
 * AND slot[AGE] >= 0.  Per-record path builds a camera-aligned billboard
 * matrix via engine FUN_00415f2e (ported below), pre-multiplies a
 * RotY(π/2) (engine line `thunk_FUN_004a3537(local_9c, 0x3fc90fdb)` —
 * thunk 3537 = RotY per math3d.h), then applies a per-type anisotropic
 * scaling.
 *
 * UV: 0x7e is a 5-frame animation (slot_idx % 5 selects a 32×32 tile in
 * a 3-wide grid starting at (80, 0)); other types use static tiles in
 * a 256×256 atlas (effect_shot.bmp).  */

int wf_pass_e_fan_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    if (type != 0x73 && type != 0x7e && type != 0x78 &&
        type != 0xa0 && type != 0x7a)
        return 0;
    /* Engine L353: `|| (piVar11[0x26] < 0) goto LAB_00417271` — AGE>=0
     * is the second gate.  Negative AGE happens during the staggered
     * spawn-burst startup for some types. */
    int32_t age = slot[SCENE1_RECORDS_B_OFF_AGE];
    return (age >= 0);
}

float wf_pass_e_fan_per_record_base_scale(const int32_t *slot)
{
    /* Engine L355: `local_c = piVar11[0x42] * 0.004` (slot[LIFE_MULT]
     * × 0.004).  No AGE ramp-in (the fan group skips the spear's L302-305
     * ramp).  0x7a applies an additional 1.2 multiplier before the
     * per-type XYZ stretch — that's reflected in scale_xyz, not here. */
    return *(const float *)&slot[SCENE1_RECORDS_B_OFF_LIFE_MULT] * 0.004f;
}

void wf_pass_e_fan_per_record_scale_xyz(const int32_t *slot,
                                        float *out_sx,
                                        float *out_sy,
                                        float *out_sz)
{
    /* Engine L359-374:
     *
     *   if (type == 0x78 || type == 0xa0) {
     *     S = (base, 2*base, 2*base);            // tall thin billboard
     *   } else if (type == 0x7a) {
     *     base *= 1.2;
     *     S = (base, 2*base, 2*base);            // 1.2× scaled variant
     *   } else {                                 // 0x73, 0x7e uniform
     *     S = (base, base, base);
     *   }
     */
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    float base = wf_pass_e_fan_per_record_base_scale(slot);
    if (type == 0x78 || type == 0xa0) {
        *out_sx = base;
        *out_sy = base * 2.0f;
        *out_sz = base * 2.0f;
        return;
    }
    if (type == 0x7a) {
        base *= 1.2f;
        *out_sx = base;
        *out_sy = base * 2.0f;
        *out_sz = base * 2.0f;
        return;
    }
    *out_sx = base;
    *out_sy = base;
    *out_sz = base;
}

void wf_pass_e_fan_uv_box(const int32_t *slot, int slot_idx,
                          float *out_u0, float *out_u1,
                          float *out_v0, float *out_v1)
{
    /* Engine L377-399 — Pass E fan UV dispatch.  Three sub-cases:
     *
     *   type == 0x7e — 5-frame animation in a 3-wide grid:
     *     col = ((slot_idx % 5) % 3) * 32 + 80
     *     row = ((slot_idx % 5) / 3) * 32
     *     tile size 32×32 (insets 0.5/31.5 → engine writes both axes
     *     with the same range).
     *
     *   type ∈ {0x78, 0xa0, 0x7a} — fixed tall tile at (96, 128):
     *     u in (96.5, 111.5) (16-px wide column)
     *     v in (128.5, 159.5) (32-px tall row)
     *
     *   else (0x73) — fixed square tile at (96, 160):
     *     u in (96.5, 111.5)
     *     v in (160.5, 175.5) (16-px tall row)
     *
     * Raw .rdata constants verified: 0.37695312 = 96.5/256, 0.43554688 =
     * 111.5/256, 0.5019531 = 128.5/256, 0.6230469 = 159.5/256,
     * 0.6269531 = 160.5/256, 0.6855469 = 175.5/256.  All in the 256×256
     * effect_shot.bmp atlas. */
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];

    if (type == 0x7e) {
        int phase = slot_idx % 5;
        float col = (float)((phase % 3) * 32 + 80);
        float row = (float)((phase / 3) * 32);
        *out_u0 = (col +  0.5f) / 256.0f;
        *out_u1 = (col + 31.5f) / 256.0f;
        *out_v0 = (row +  0.5f) / 256.0f;
        *out_v1 = (row + 31.5f) / 256.0f;
        return;
    }

    *out_u0 = 96.5f  / 256.0f;     /* 0.37695312 */
    *out_u1 = 111.5f / 256.0f;     /* 0.43554688 */

    if (type == 0x78 || type == 0xa0 || type == 0x7a) {
        *out_v0 = 128.5f / 256.0f; /* 0.5019531 */
        *out_v1 = 159.5f / 256.0f; /* 0.6230469 */
        return;
    }

    /* 0x73 default. */
    *out_v0 = 160.5f / 256.0f;     /* 0.6269531 */
    *out_v1 = 175.5f / 256.0f;     /* 0.6855469 */
}

void wf_pass_e_fan_billboard_matrix(float out[16], const int32_t *slot,
                                    const float camera_eye[3])
{
    /* Engine FUN_00415f2e @ 0x415f2e (125 B):
     *
     *   eye    = (POS_X,  POS_Y,  POS_Z)              // local_28
     *   target = (POS_X+VEL_X, POS_Y+VEL_Y, POS_Z+VEL_Z)  // local_1c
     *   up     = (cam.x-POS_X, cam.y-POS_Y, cam.z-POS_Z)  // local_10
     *   D3DXMatrixLookAtRH(out, eye, target, up);
     *   D3DXMatrixInverse(out, NULL, out);
     *
     * Byte-offset reading confirms (param_1 + 0x68 = 26 dw = VEL_X, +0x6c
     * = VEL_Y, +0x70 = VEL_Z; +0x5c = 23 dw = POS_X).  _DAT_073de31c..324
     * is g_scene1_camera_eye (3 floats) — caller passes it in to keep the
     * helper testable without scene1_camera linkage.
     *
     * Result: world-space matrix that places a local XY-plane quad
     * oriented to face the camera along the velocity direction (the
     * billboard's local +Z = velocity direction; +Y axis aligned with
     * the camera-relative up).  Singular if eye == target (zero velocity)
     * OR if (camera - pos) is parallel to velocity; the engine doesn't
     * guard either case — the resulting matrix has NaN/inf entries which
     * pass through SetTransform fine (the draw is just invisible).  We
     * preserve that behavior; mat4_inverse's singular-return signal is
     * ignored. */
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float vel_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_VEL_X];
    float vel_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_VEL_Y];
    float vel_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_VEL_Z];

    float eye[3]    = { pos_x,         pos_y,         pos_z         };
    float target[3] = { pos_x + vel_x, pos_y + vel_y, pos_z + vel_z };
    float up[3]     = { camera_eye[0] - pos_x,
                        camera_eye[1] - pos_y,
                        camera_eye[2] - pos_z };

    float lookat[16];
    mat4_lookat_rh(lookat, eye, target, up);
    (void)mat4_inverse(out, lookat);
}

void wf_pass_e_fan_compose_world(float out[16], const int32_t *slot,
                                 const float camera_eye[3])
{
    /* Engine L356-376 chained on top of FUN_00415f2e:
     *
     *   M = camera_billboard(slot)                      // local_5c
     *   RY = RotY(π/2)                                  // local_9c
     *   M  = RY * M                                     // Multiply(M, RY, M)
     *   S  = per-type scaling                           // local_X
     *   M  = S * M                                      // Multiply(M, S, M)
     *
     * Final world matrix = S × RotY(π/2) × billboard, applied to a local
     * XY-plane quad.  */
    wf_pass_e_fan_billboard_matrix(out, slot, camera_eye);

    float scratch[16];
    mat4_rotation_y(scratch, 1.5707964f /* 0x3fc90fdb = π/2 */);
    mat4_mul(out, scratch, out);

    float sx, sy, sz;
    wf_pass_e_fan_per_record_scale_xyz(slot, &sx, &sy, &sz);
    mat4_scaling(scratch, sx, sy, sz);
    mat4_mul(out, scratch, out);
}

/* ═══ Pass D: item-pickup billboard walker (C8f.pass-d) ═══════════════════
 *
 * Engine FUN_004161c7 L224-287.  Walks g_scene1_records_c (stride 0x25 —
 * same table as Pass C!) filtered by TYPE != -1 AND TYPE > 6 (cardinal
 * int, denormal-float comparison in decomp).  These are world pickups /
 * item drops with TYPE > 6; Pass C handles TYPE ∈ {0,1,2,3} (jems/coins).
 *
 * Per-record draw involves:
 *   1. Position-only translation (no per-record rotation).
 *   2. Optional pulse RGB if this slot is the player-selected pickup.
 *   3. State-driven alpha (state==2 pickup-bob fades in from age 0x1e).
 *   4. Item-database texture/tile lookup via FUN_004681f6 stand-in.
 *   5. Per-tile UV box (32×32 tiles on a per-item texture atlas of
 *      width 256, custom height).
 *
 * The texture lookup is the only HOUSE-dormant piece by design: the item
 * database (DAT_095d3804 at stride 0xb3 dw, parsed from data/item.txt at
 * line 73886) is unported.  The walker exposes a resolver hook
 * `wf_pass_d_set_item_resolver()` defaulting to a miss-stub; without a
 * real resolver, every record skips emit — preserving byte-identical
 * HOUSE behavior whether table C is populated or not.
 *
 * The Ghidra-dropped __ftol arg at engine L13640 was resolved via raw
 * asm (`fmul ds:0x51938c ; fadd ds:0x51937c ; call 0x503954`) — the
 * formula is `sinf(angle) * 64.0 + 96.0`, producing rgb in [32, 160].
 * 0x5194ec = 0.3 (pre-sin angle scale, Ghidra-visible).  */

/* Pass D filter: TYPE != -1 AND TYPE > 6.  Engine L231:
 *
 *   if ((*local_8 != -NAN) && (6 < (int)*local_8))
 *
 * `-NAN` is the 0xFFFFFFFF int sentinel; cardinal-int compare reads the
 * slot directly (no denormal-float trick). */
int wf_pass_d_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_C_OFF_TYPE];
    if (type == -1) return 0;
    return (type > 6);
}

/* Pass D per-record scale.  Engine L232-233 + L640-642:
 *
 *   local_10 = 0.0192;   local_c = 0.0192;
 *   if (local_18 == DAT_056dae40) {
 *     ...
 *     local_10 = 0.026880002;  local_c = 0.026880002;
 *   }
 *
 * Selected-slot path swaps to a slightly larger scale (~1.4× larger,
 * giving the highlighted pickup a subtle "puff up" feel as it pulses).
 * Same .rdata literal as Pass C's middle bucket. */
float wf_pass_d_per_record_scale(int is_selected)
{
    return is_selected ? 0.026880002f : 0.0192f;
}

/* Pass D pulse RGB.  Engine L13637-13642 + raw asm @ 0x416ae1..0x416b0c:
 *
 *   angle  = (int)slot[AGE] * 0.3                  (0x5194ec = 0.3)
 *   flash  = sinf(angle)                           (FUN_00503a44)
 *   value  = flash * 64.0 + 96.0                   (0x51938c=64, 0x51937c=96)
 *   rgb_lo = (uint32_t)__ftol(value)               (FUN_00503954)
 *
 * Range [32, 160] (since sinf ∈ [-1, 1]).  __ftol does truncating
 * float→int32 — the engine's CRT fast-path matches `(int)value` for
 * positive values in this range.  Returned as uint32 for the diffuse
 * shuffle.  Only computed when is_selected; default 0 otherwise. */
uint32_t wf_pass_d_pulse_rgb(int32_t age, int is_selected)
{
    if (!is_selected) return 0;
    float angle = (float)age * 0.3f;
    float flash = sinf(angle);
    float value = flash * 64.0f + 96.0f;
    return (uint32_t)(int32_t)value;
}

/* Pass D alpha.  Engine L13644-13651 reads slot[STATE] (denormal-float
 * compare against 2.8026e-45 = bit-cast 2):
 *
 *   alpha = 0xff;
 *   if (slot[STATE] == 2) {
 *     alpha = 0;
 *     if (slot[AGE] > 0x1e) {
 *       alpha = (slot[AGE] - 0x1e) * 0x20;
 *       if (alpha > 0xff) alpha = 0xff;
 *     }
 *   }
 *
 * STATE==2 is the pickup-bob branch (C8j.2 _spawn_pickup sets it);
 * STATE==0 is world-drop physics (kept fully opaque).  The pickup-bob
 * fades in over ~8 frames: age 0x1e .. 0x25 ramps alpha 0 → 0xff in
 * 0x20 (32) increments per frame. */
int wf_pass_d_alpha(const int32_t *slot)
{
    int32_t state = slot[SCENE1_RECORDS_C_OFF_STATE];
    int32_t age   = slot[SCENE1_RECORDS_C_OFF_AGE];
    if (state != 2) return 0xff;
    if (age <= 0x1e) return 0;
    int alpha = (age - 0x1e) * 0x20;
    if (alpha > 0xff) alpha = 0xff;
    return alpha;
}

/* Pass D ARGB diffuse.  Engine L13654:
 *
 *   diffuse = ((alpha << 8 | rgb) << 8 | rgb) << 8 | rgb
 *
 * Three identical RGB channels (grayscale) with separately-computed
 * alpha.  Channel layout is 0xAARRGGBB — D3D's standard ARGB layout. */
uint32_t wf_pass_d_diffuse(uint32_t rgb_lo, int alpha)
{
    return ((((uint32_t)alpha) << 24)
            | ((rgb_lo & 0xffu) << 16)
            | ((rgb_lo & 0xffu) <<  8)
            |  (rgb_lo & 0xffu));
}

/* Pass D tile-in-atlas → UV box.  Engine L13666-13682:
 *
 *   tile_raw  = item.tile (or 0 if slot[PICKUP_E1] != 0)
 *   col       = tile_raw % 8
 *   row       = tile_raw / 8
 *   u0 = (col*32 + 0.5)  / 256.0
 *   u1 = (col*32 + 31.5) / 256.0
 *   v0 = (row*32 + 0.5)  / tex_height
 *   v1 = (row*32 + 31.0) / tex_height        // NB: 31.0, NOT 31.5
 *
 * The asymmetric v1 inset (31.0 vs 31.5) is engine-verbatim — same shape
 * as Pass C's `63.0` quirk.  Width is hardcoded 256 (the item atlases
 * are uniformly 256-px wide); height varies per atlas, fed in from the
 * texture-bank table (DAT_073d8780). */
void wf_pass_d_uv_box(int tile_raw, float tex_height,
                      float *out_u0, float *out_u1,
                      float *out_v0, float *out_v1)
{
    int col = tile_raw % 8;
    int row = tile_raw / 8;
    *out_u0 = ((float)col * 32.0f +  0.5f) / 256.0f;
    *out_u1 = ((float)col * 32.0f + 31.5f) / 256.0f;
    *out_v0 = ((float)row * 32.0f +  0.5f) / tex_height;
    *out_v1 = ((float)row * 32.0f + 31.0f) / tex_height;
}

/* Pass D world matrix.  Engine L13634 + L13657-13659:
 *
 *   Translation(M, POS_X, POS_Y, POS_Z);
 *   Scaling(S, scale, scale, scale);
 *   M = S * M;                     // Multiply(M, S, M) — actually
 *                                  //   thunk_FUN_004a2a03(local_5c, local_9c)
 *                                  //   with implicit out=in_a (2-arg form)
 *   M = DAT_0438cdf8 * M;          // Multiply(M, DAT_0438cdf8, M)
 *
 * Engine line L13658's `thunk_FUN_004a2a03(local_5c, local_9c)` is the
 * 2-arg D3DXMatrixMultiply convention where out and the first operand
 * alias — i.e. `local_5c = local_9c * local_5c`.  Same shape as Pass C.
 *
 * Reuses Pass C's pre-matrix stand-in (also driven by DAT_0438cdf8) —
 * setter/getter shared.  Default identity is benign. */
void wf_pass_d_compose_world(float out[16], const int32_t *slot,
                             int is_selected)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_C_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_C_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_C_OFF_POS_Z];
    float scale = wf_pass_d_per_record_scale(is_selected);

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, scale, scale, scale);
    mat4_mul(out, scratch, out);

    mat4_mul(out, wf_pass_c_get_pre_matrix(), out);
}

/* Pass D item resolver hook (stand-in for FUN_004681f6 + table walks).
 *
 * Default returns 0 (miss) → walker skips emit per record.  When the
 * item-database port lands (data/item.txt parser at all.c L73886 +
 * DAT_095d3804 + DAT_073d8778 texture-bank table), wire a real resolver
 * via wf_pass_d_set_item_resolver().  The hook is type-key driven, where
 * type_key = slot[TYPE] - 7 (engine L13660: `(int)*local_8 + -7`).  */
static int wf_pass_d_resolver_default(int type_key,
                                      wf_pass_d_item_resolved *out)
{
    (void)type_key;
    (void)out;
    return 0;
}

static wf_pass_d_item_resolver_fn g_wf_pass_d_resolver =
    wf_pass_d_resolver_default;

wf_pass_d_item_resolver_fn wf_pass_d_set_item_resolver(
    wf_pass_d_item_resolver_fn fn)
{
    wf_pass_d_item_resolver_fn prev = g_wf_pass_d_resolver;
    g_wf_pass_d_resolver = fn ? fn : wf_pass_d_resolver_default;
    return prev;
}

int wf_pass_d_resolve_item(int type_key, wf_pass_d_item_resolved *out)
{
    if (!out) return 0;
    out->tex = NULL;
    out->tile_raw = 0;
    out->tex_height = 1;
    return g_wf_pass_d_resolver(type_key, out);
}

/* Pass D selected-slot index (engine DAT_056dae40).
 *
 * Engine writers: set to -1 at init (L34551 + L90413).  Assigned a real
 * slot index at L90463 inside an item-pickup UI path (FUN_004676d4).
 * When the player is hovering over a pickup, this points to its slot;
 * Pass D pulses that slot specifically.  Default -1 → no record matches
 * the `local_18 == DAT_056dae40` test → no pulse.  */
int g_wf_pass_d_selected_slot = -1;
