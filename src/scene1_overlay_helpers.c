/*
 * scene1_overlay_helpers.c — D3D-free helpers for the FUN_00414ee2 2D
 * overlay dispatcher (chip O.3).
 *
 * Same split as scene1_wide_followup_helpers.c: the algebraic per-record
 * helpers (gate cascade, alpha compute, scale, world matrix, UV box,
 * diffuse encoding) live here so host unit tests can link them without
 * pulling in <d3d8.h>.  scene1_overlay.c itself stays #ifdef _WIN32 for
 * the actual dispatcher entry (SetTransform / SetTexture / DrawPrimitiveUP).
 *
 * O.3 covers shape 0/5 (the simplest single-quad T × S × pre_matrix
 * path).  Chips O.4..O.7 will add helpers for the remaining shapes.
 *
 * Engine constants surfaced (.rdata, confirmed via objdump):
 *   0x519320 = 0.0f
 *   0x51935c = 0.5f
 *   0x519390 = 256.0f         — atlas-px → normalised UV divisor
 *   0x519434 = 1.5707963f     — π/2
 *   0x519630 = 255.0f         — alpha range
 *   0x5198a0 = 0.003f         — shape 0/5 scale constant
 *
 * Survey correction landed by O.3: the survey doc claimed shape 0/5 does
 * a "vertical flip if (slot.rng_seed & 1) != 0", but the asm at
 * 0x415a2e is `test [ebp-0x20], 0x1` — and [ebp-0x20] is the inner
 * slot scan counter (local_24), NOT the slot's rng_seed.  It's a
 * HORIZONTAL flip based on the slot's table position (the inner scan
 * idx), which means adjacent slots of the same template alternate
 * facing.  Updated comments in this TU + the spawn API header.
 */

#include "scene1_overlay.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "math3d.h"
#include "scene1_wide_followup.h"  /* wf_pass_c_get_pre_matrix() — shared pre-matrix stand-in */

/* ---- Shape entry accessor ----------------------------------------- */

static const int32_t *overlay_shape_entry(int texture_type)
{
    if (texture_type < 0 || texture_type >= SCENE1_OVERLAY_SHAPE_COUNT) {
        return NULL;
    }
    return &g_scene1_overlay_shapes[texture_type * SCENE1_OVERLAY_SHAPE_STRIDE];
}

static float bits_to_f(int32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static float slot_get_f(const int32_t *slot, int off)
{
    return bits_to_f(slot[off]);
}

/* ---- Gate cascade ------------------------------------------------- */

int scene1_overlay_should_emit(const int32_t *slot,
                               int param_layer, int param_mode,
                               int outer_idx)
{
    if (!slot) return 0;
    if (slot[SCENE1_OVERLAY_OFF_ACTIVE] == -1) return 0;
    if (slot[SCENE1_OVERLAY_OFF_LAYER]  != param_layer) return 0;
    if (slot[SCENE1_OVERLAY_OFF_MODE]   != param_mode)  return 0;

    int32_t texture_type = slot[SCENE1_OVERLAY_OFF_TEXTURE_TYPE];
    const int32_t *shape = overlay_shape_entry(texture_type);
    if (!shape) return 0;   /* OOB texture_type — engine indexes blindly,
                              we clamp.  All-zero shape rows have
                              tex_group == 0 → only match outer_idx == 0. */
    if (shape[SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP] != outer_idx) return 0;

    if (slot[SCENE1_OVERLAY_OFF_AGE] < 0) return 0;
    return 1;
}

/* ---- Fade compute ------------------------------------------------- */

int scene1_overlay_compute_fade(const int32_t *slot,
                                int *out_alpha_int,
                                float *out_alpha_mix)
{
    int   fade_in_dur     = slot[SCENE1_OVERLAY_OFF_FADE_IN_DUR];
    int   age             = slot[SCENE1_OVERLAY_OFF_AGE];
    int   shape_mode      = slot[SCENE1_OVERLAY_OFF_SHAPE_MODE];
    float unk_48          = slot_get_f(slot, SCENE1_OVERLAY_OFF_UNK_48);
    int   fade_out_dur    = slot[SCENE1_OVERLAY_OFF_FADE_OUT_DUR];
    int   fade_out_offset = slot[SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET];
    int   age_birth       = slot[SCENE1_OVERLAY_OFF_AGE_BIRTH];
    uint8_t blend_byte    = (uint8_t)(slot[SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE] & 0xff);

    float alpha     = 255.0f;
    float color_val = 255.0f;
    float alpha_mix = 1.0f;

    if (fade_in_dur > 0) {
        alpha = ((float)age * 255.0f) / (float)fade_in_dur;
        if (alpha > 255.0f) alpha = 255.0f;
    }

    int skip_fade_out = (shape_mode == 4 && unk_48 != 0.0f);
    if (!skip_fade_out && fade_out_dur > 0) {
        int delta = age - age_birth;
        if ((fade_out_offset - fade_out_dur) < delta) {
            /* Engine uses integer division (idiv) on 255 / fade_out_dur,
             * then converts to float via fild.  Match the truncation. */
            int step_i = 255 / fade_out_dur;
            int adj    = (delta - fade_out_offset) + fade_out_dur;
            alpha -= (float)adj * (float)step_i;
        }
    }

    if (alpha < 0.0f) return 0;

    if (blend_byte == 0 || blend_byte == 2) color_val = alpha;
    if (blend_byte == 1 || blend_byte == 2) alpha_mix = alpha / 255.0f;

    /* Engine `call 0x503954` (__ftol) truncates toward zero.  C cast
     * matches for positive inputs (which color_val is here — we already
     * gated on alpha >= 0 and the blend-byte branches only copy alpha
     * or keep 255). */
    if (out_alpha_int) *out_alpha_int = (int)color_val;
    if (out_alpha_mix) *out_alpha_mix = alpha_mix;
    return 1;
}

/* ---- Shape 0/5 scale ---------------------------------------------- */

void scene1_overlay_shape_05_scale(const int32_t *slot,
                                   float alpha_mix,
                                   float *out_sx, float *out_sy)
{
    float blend_mix  = slot_get_f(slot, SCENE1_OVERLAY_OFF_BLEND_MIX);
    float scale_base = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_BASE);
    float scale_x    = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_X);

    float common = scale_base * alpha_mix * scale_x * 0.003f / 0.5f;
    if (out_sx) *out_sx = (1.0f - blend_mix) * common;
    if (out_sy) *out_sy = blend_mix * common;
}

/* ---- Shape 0/5 world matrix --------------------------------------- */

void scene1_overlay_shape_05_compose_world(float out[16],
                                           const int32_t *slot,
                                           float alpha_mix)
{
    float pos_x = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_X);
    float pos_y = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Y);
    float pos_z = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Z);

    float sx, sy;
    scene1_overlay_shape_05_scale(slot, alpha_mix, &sx, &sy);

    float scratch[16];

    /* T (translation) into out. */
    mat4_translation(out, pos_x, pos_y, pos_z);

    /* S = scaling(sx, sy, sx) — engine calls scaling(sx, sy, sx)
     * (third arg is sx, NOT a third independent axis). */
    mat4_scaling(scratch, sx, sy, sx);

    /* world = S × T (mat4_mul is left-multiply: out = left × right). */
    mat4_mul(out, scratch, out);

    /* world = pre_matrix × world (same DAT_0438cdf8 stand-in as
     * wide_followup Pass C/D and Pass E spear). */
    mat4_mul(out, wf_pass_c_get_pre_matrix(), out);
}

/* ---- Frame UV selection ------------------------------------------- */

void scene1_overlay_shape_05_frame_uv(const int32_t *shape_entry,
                                      int rng_seed,
                                      float *out_uv_origin_x,
                                      float *out_uv_origin_y)
{
    if (!shape_entry) {
        if (out_uv_origin_x) *out_uv_origin_x = 0.0f;
        if (out_uv_origin_y) *out_uv_origin_y = 0.0f;
        return;
    }

    float uv_origin_x = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_X]);
    float uv_origin_y = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_Y]);
    float uv_size_x   = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]);
    float uv_size_y   = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]);
    int   frame_count = shape_entry[SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT];

    float u = uv_origin_x;
    float v = uv_origin_y;

    if (frame_count > 1) {
        /* Engine: frames_per_row = (int)(256.0 / uv_size_x).  __ftol
         * truncates toward zero.  Then rng_seed is partitioned by
         * idiv (signed) into (col, row).  Negative rng_seed gives
         * negative column / row offsets — matches the engine's
         * verbatim signed-int division. */
        int frames_per_row = (int)(256.0f / uv_size_x);
        if (frames_per_row != 0) {
            int col = rng_seed % frames_per_row;
            int row = rng_seed / frames_per_row;
            u = (float)col * uv_size_x + uv_origin_x;
            v = (float)row * uv_size_y + uv_origin_y;
        }
    }

    if (out_uv_origin_x) *out_uv_origin_x = u;
    if (out_uv_origin_y) *out_uv_origin_y = v;
}

/* ---- Diffuse gray encoding ---------------------------------------- */

uint32_t scene1_overlay_diffuse_gray(int alpha_int)
{
    uint32_t g = (uint32_t)alpha_int;
    uint32_t a = g | 0xffffff00u;
    a = (a << 8) | g;
    a = (a << 8) | g;
    return a;
}

/* ---- Shape 0/5 quad emit ------------------------------------------ */

void scene1_overlay_shape_05_emit_quad(scene1_overlay_vertex vbuf[4],
                                       const int32_t *shape_entry,
                                       float uv_origin_x, float uv_origin_y,
                                       int slot_idx,
                                       int alpha_int)
{
    if (!vbuf) return;

    float uv_size_x = 0.0f, uv_size_y = 0.0f;
    if (shape_entry) {
        uv_size_x = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]);
        uv_size_y = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]);
    }

    /* Engine asm 0x415a25..0x415a78 — slot_idx & 1 horizontal flip.
     * Two values get assigned to slot pair (b760/b790) but the
     * assignment ORDER swaps based on slot_idx parity.  Survey doc
     * had this attributed to slot.rng_seed; corrected here. */
    float u_a = (uv_origin_x + 0.5f) / 256.0f;
    float u_b = (uv_origin_x + uv_size_x - 0.5f) / 256.0f;

    float v_top    = (uv_origin_y + 0.5f) / 256.0f;
    float v_bottom = (uv_origin_y + uv_size_y - 0.5f) / 256.0f;

    /* Map engine static-vbuf layout to our (v0..v3):
     *   v0 @ 0x76b750  pos = (-256, +256, 0)
     *   v1 @ 0x76b768  pos = (-256, -256, 0)
     *   v2 @ 0x76b780  pos = (+256, +256, 0)
     *   v3 @ 0x76b798  pos = (+256, -256, 0)
     * UV slots: v0=(b760/b764), v1=(b778/b77c), v2=(b790/b794),
     *           v3=(b7a8/b7ac).  Engine assigns b778=b760, b7a8=b790;
     *           v0 and v1 share U, v2 and v3 share U. */
    float u_v0_v1, u_v2_v3;
    if ((slot_idx & 1) == 0) {
        u_v0_v1 = u_b;   /* even slot_idx: vert 0 (b760) = u_right */
        u_v2_v3 = u_a;   /* vert 2 (b790) = u_left */
    } else {
        u_v0_v1 = u_a;   /* odd slot_idx: swap */
        u_v2_v3 = u_b;
    }

    uint32_t diffuse = scene1_overlay_diffuse_gray(alpha_int);

    vbuf[0].diffuse = diffuse;
    vbuf[1].diffuse = diffuse;
    vbuf[2].diffuse = diffuse;
    vbuf[3].diffuse = diffuse;

    vbuf[0].u = u_v0_v1; vbuf[0].v = v_top;
    vbuf[1].u = u_v0_v1; vbuf[1].v = v_bottom;
    vbuf[2].u = u_v2_v3; vbuf[2].v = v_top;
    vbuf[3].u = u_v2_v3; vbuf[3].v = v_bottom;
}

/* ---- Shapes 2/3/4/6: uniform scale -------------------------------- */

float scene1_overlay_shape_2346_uniform_scale(const int32_t *slot,
                                              float alpha_mix)
{
    float scale_base = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_BASE);
    float scale_x    = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_X);
    /* Engine asm 0x4156a0..0x4156b3:
     *   fld [scale_base]; fmul [scale_x]; fmul [alpha_mix]; fmul 0.003
     * No /0.5; no blend split.  */
    return scale_base * scale_x * alpha_mix * 0.003f;
}

/* Shared S × T core for shapes 2/3/4/6.  Reads pos.x/y/z and the
 * uniform scale; writes out = scaling(s, s, s) × translation(pos).  */
static void shape_2346_st_core(float out[16],
                               const int32_t *slot,
                               float alpha_mix)
{
    float pos_x = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_X);
    float pos_y = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Y);
    float pos_z = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Z);
    float s     = scene1_overlay_shape_2346_uniform_scale(slot, alpha_mix);

    float scratch[16];
    mat4_translation(out, pos_x, pos_y, pos_z);
    mat4_scaling(scratch, s, s, s);
    mat4_mul(out, scratch, out);
}

/* ---- Shape 2: pre_matrix × (S × T) -------------------------------- */

void scene1_overlay_shape_2_compose_world(float out[16],
                                          const int32_t *slot,
                                          float alpha_mix)
{
    shape_2346_st_core(out, slot, alpha_mix);
    mat4_mul(out, wf_pass_c_get_pre_matrix(), out);
}

/* ---- Shape 3: RotX × RotY × RotZ × (S × T) ------------------------ */

void scene1_overlay_shape_3_compose_world(float out[16],
                                          const int32_t *slot,
                                          float alpha_mix)
{
    shape_2346_st_core(out, slot, alpha_mix);

    /* Engine asm 0x415745..0x4157b5 — applies rotations in order:
     *   world = RotZ(slot[0x40]) × world
     *   world = RotY(slot[0x38]) × world   (off-diagonal: "rot.x" slot drives Y)
     *   world = RotX(slot[0x3c]) × world   (off-diagonal: "rot.y" slot drives X)
     * Final composition: RotX × RotY × RotZ × S × T. */
    float rz_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_Z);  /* slot[+0x40] */
    float ry_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_X);  /* slot[+0x38] — "rot.x" → RotY */
    float rx_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_Y);  /* slot[+0x3c] — "rot.y" → RotX */

    float scratch[16];
    mat4_rotation_z(scratch, rz_val);
    mat4_mul(out, scratch, out);
    mat4_rotation_y(scratch, ry_val);
    mat4_mul(out, scratch, out);
    mat4_rotation_x(scratch, rx_val);
    mat4_mul(out, scratch, out);
}

/* ---- Shape 4: RotY(π/2) × (S × T) --------------------------------- */

void scene1_overlay_shape_4_compose_world(float out[16],
                                          const int32_t *slot,
                                          float alpha_mix)
{
    shape_2346_st_core(out, slot, alpha_mix);

    /* Engine asm 0x41571b..0x41573d — hard-coded RotY(π/2) via
     * .rdata 0x519434 = 1.5707963f. */
    float scratch[16];
    mat4_rotation_y(scratch, 1.5707963f);
    mat4_mul(out, scratch, out);
}

/* ---- Shape 6: RotX(slot[+0x3c]) × (S × T) ------------------------- */

void scene1_overlay_shape_6_compose_world(float out[16],
                                          const int32_t *slot,
                                          float alpha_mix)
{
    shape_2346_st_core(out, slot, alpha_mix);

    /* Engine asm 0x4157b7..0x4157d3 — `fld [ebx+0x3c]; call 0x4a35ef`
     * (0x4a35ef = RotationX short-jmp).  Field is Ghidra-named "rot.y"
     * but the engine applies it as a RotX value.  */
    float rx_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_Y);
    float scratch[16];
    mat4_rotation_x(scratch, rx_val);
    mat4_mul(out, scratch, out);
}

/* ---- Shapes 1/2/3/4/6: non-flipped UV + diffuse emit -------------- */

void scene1_overlay_shape_1346_emit_quad(scene1_overlay_vertex vbuf[4],
                                         const int32_t *shape_entry,
                                         float uv_origin_x, float uv_origin_y,
                                         int alpha_int)
{
    if (!vbuf) return;

    float uv_size_x = 0.0f, uv_size_y = 0.0f;
    if (shape_entry) {
        uv_size_x = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]);
        uv_size_y = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]);
    }

    /* Engine asm 0x41587d..0x4158be — same UV box as shape 0/5 but
     * always v0/v1 = u_left, v2/v3 = u_right (no slot_idx parity flip). */
    float u_left   = (uv_origin_x + 0.5f) / 256.0f;
    float u_right  = (uv_origin_x + uv_size_x - 0.5f) / 256.0f;
    float v_top    = (uv_origin_y + 0.5f) / 256.0f;
    float v_bottom = (uv_origin_y + uv_size_y - 0.5f) / 256.0f;

    uint32_t diffuse = scene1_overlay_diffuse_gray(alpha_int);
    vbuf[0].diffuse = diffuse;
    vbuf[1].diffuse = diffuse;
    vbuf[2].diffuse = diffuse;
    vbuf[3].diffuse = diffuse;

    vbuf[0].u = u_left;  vbuf[0].v = v_top;
    vbuf[1].u = u_left;  vbuf[1].v = v_bottom;
    vbuf[2].u = u_right; vbuf[2].v = v_top;
    vbuf[3].u = u_right; vbuf[3].v = v_bottom;
}

/* ---- Shape 1: lookat billboard (O.5) ------------------------------ */

float scene1_overlay_shape_1_extra_scale(const int32_t *slot)
{
    int age       = slot[SCENE1_OVERLAY_OFF_AGE];
    int age_birth = slot[SCENE1_OVERLAY_OFF_AGE_BIRTH];
    int delta     = age - age_birth;

    if (delta < 0) return 0.0f;

    float extra = 0.02f;
    if (delta > 8) {
        extra = 0.02f - (float)(delta - 8) * 0.001f;
        if (extra <= 0.0f) return 0.0f;
    }
    return extra;
}

void scene1_overlay_shape_1_scale_xyz(const int32_t *slot,
                                      float alpha_mix,
                                      float extra,
                                      float *out_sx,
                                      float *out_sy,
                                      float *out_sz)
{
    float blend_mix  = slot_get_f(slot, SCENE1_OVERLAY_OFF_BLEND_MIX);
    float scale_base = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_BASE);
    float scale_x    = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_X);

    /* sx = (1-mix) * sb * am * sx_slot * 0.0588 / 0.5 * extra
     * (.rdata 0x519948 = 0.0588; the / 0.5 is the same divisor as shape
     * 0/5).  The * extra at the tail comes from the asm at 0x415434
     * (fmul [ebp-0x10]) where [ebp-0x10] is the extra fade-scale. */
    float sx = (1.0f - blend_mix) * scale_base * alpha_mix * scale_x
               * 0.0588f / 0.5f * extra;

    /* sy = mix * sb * am * sx_slot * 1.386 / 0.5 * 0.015
     * (.rdata 0x519944 = 1.386; 0x519940 = 0.015).  Note the trailing
     * * 0.015 is unconditional (does NOT multiply by extra). */
    float sy = blend_mix * scale_base * alpha_mix * scale_x
               * 1.386f / 0.5f * 0.015f;

    if (out_sx) *out_sx = sx;
    if (out_sy) *out_sy = sy;
    if (out_sz) *out_sz = sy + sy;   /* engine writes `2 * sy` */
}

void scene1_overlay_shape_1_compose_world(float out[16],
                                          const int32_t *slot,
                                          float alpha_mix,
                                          float extra,
                                          const float camera_eye[3])
{
    float pos[3] = {
        slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_X),
        slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Y),
        slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Z),
    };
    float bend[3] = {
        slot_get_f(slot, SCENE1_OVERLAY_OFF_BEND_X),
        slot_get_f(slot, SCENE1_OVERLAY_OFF_BEND_Y),
        slot_get_f(slot, SCENE1_OVERLAY_OFF_BEND_Z),
    };
    /* target = pos + bend (engine writes local_9c/98/94 from
     * bend.xyz + pos.xyz at 0x41545b..0x415479) */
    float target[3] = { pos[0] + bend[0], pos[1] + bend[1], pos[2] + bend[2] };

    /* up = camera_eye - pos (engine asm 0x41547f..0x4154a0) */
    float up[3] = {
        camera_eye[0] - pos[0],
        camera_eye[1] - pos[1],
        camera_eye[2] - pos[2],
    };

    /* lookat then inverse — same pattern as Pass E fan billboard
     * (scene1_wide_followup_helpers.c wf_pass_e_fan_billboard_matrix). */
    float lookat[16];
    mat4_lookat_rh(lookat, pos, target, up);
    mat4_inverse(out, lookat);

    /* scale = scaling(sx, sy, 2*sy) */
    float sx, sy, sz;
    scene1_overlay_shape_1_scale_xyz(slot, alpha_mix, extra, &sx, &sy, &sz);

    /* RotY(π/2) — engine asm 0x4154f5..0x415506 (.rdata 0x519434
     * = 1.5707963f).  Apply BEFORE scale (left-mul order asm shows:
     *   world = RotY × world (0x415516..0x41551e)
     *   world = scale × world (0x415527..0x41552e)
     * so final composition = scale × RotY × inverse(lookat)). */
    float scratch[16];
    mat4_rotation_y(scratch, 1.5707963f);
    mat4_mul(out, scratch, out);

    mat4_scaling(scratch, sx, sy, sz);
    mat4_mul(out, scratch, out);
}
