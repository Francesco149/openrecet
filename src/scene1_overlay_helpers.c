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
 * slot scan counter (local_24), NOT the slot's anim_cell_index (which
 * PFO.0 confirmed isn't an RNG seed at all, just the per-cell anim
 * counter).  It's a HORIZONTAL flip based on the slot's table position
 * (the inner scan idx), which means adjacent slots of the same template
 * alternate facing.  Updated comments in this TU + the spawn API header.
 */

#include "scene1_overlay.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "math3d.h"
#include "scene1_wide_followup.h"  /* wf_pass_c_get_pre_matrix() — shared pre-matrix stand-in */

#ifndef TWO_PI_F
#define TWO_PI_F 6.2831855f
#endif

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
                                      int anim_cell_index,
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
         * truncates toward zero.  Then anim_cell_index is partitioned by
         * idiv (signed) into (col, row).  Negative anim_cell_index gives
         * negative column / row offsets — matches the engine's
         * verbatim signed-int division. */
        int frames_per_row = (int)(256.0f / uv_size_x);
        if (frames_per_row != 0) {
            int col = anim_cell_index % frames_per_row;
            int row = anim_cell_index / frames_per_row;
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

/* ---- Shape 7: multi-quad strip trail (O.6) ------------------------ */

/* Static vbuf — 33 pairs (66 verts) of a 1/4-arc strip in YZ, swept
 * across 0..π/2 in 33 steps, with verts at X = ±48.  Positions from
 * engine FUN_0040d132 L333-354 .data init.  Diffuse/UV are placeholders
 * (overwritten by scene1_overlay_shape_7_emit_strip per draw).  */
scene1_overlay_vertex
    g_scene1_overlay_shape_7_vbuf[SCENE1_OVERLAY_SHAPE_7_VERT_COUNT];

void scene1_overlay_shape_7_vbuf_init(void)
{
    /* Engine FUN_0040d132 L333-354: 33 iterations writing 2 verts each.
     *   angle = i * (π/2) / 32
     *   vert A: pos = (+48, sin(angle)*1024, (cos(angle)-1)*1024)
     *   vert B: pos = (-48, sin(angle)*1024, (cos(angle)-1)*1024)
     *
     * Placeholder UV/diffuse from the engine init kept verbatim — they
     * get overwritten in scene1_overlay_shape_7_emit_strip.  */
    for (int i = 0; i < SCENE1_OVERLAY_SHAPE_7_PAIR_COUNT; i++) {
        float angle = (float)i * 1.5707964f / 32.0f;
        float sa = sinf(angle);
        float ca = cosf(angle);
        float y = sa * 1024.0f;
        float z = (ca - 1.0f) * 1024.0f;
        scene1_overlay_vertex *va = &g_scene1_overlay_shape_7_vbuf[i * 2 + 0];
        scene1_overlay_vertex *vb = &g_scene1_overlay_shape_7_vbuf[i * 2 + 1];
        va->x = 48.0f;  va->y = y; va->z = z;
        vb->x = -48.0f; vb->y = y; vb->z = z;
        /* Placeholder UV from engine init (loop variant): vert A uses
         * (0.5004883, 0.18847656), vert B uses (0.5620117, 0.24902344).
         * These are overwritten per draw but the init writes them. */
        va->u = 0.5004883f;  va->v = 0.18847656f;
        vb->u = 0.5620117f;  vb->v = 0.24902344f;
        va->diffuse = 0xFFFFFFFFu;
        vb->diffuse = 0xFFFFFFFFu;
    }
}

int scene1_overlay_shape_7_compute_strip(const int32_t *slot,
                                         int alpha_int_in,
                                         int *out_vert_count,
                                         int *out_pair_start,
                                         int *out_fade_gray)
{
    if (out_vert_count) *out_vert_count = 0;
    if (out_pair_start) *out_pair_start = 0;
    if (out_fade_gray)  *out_fade_gray  = 0;

    int age = slot[SCENE1_OVERLAY_OFF_AGE];

    /* Engine asm 0x415085..0x415090 — AGE in [0, 0x28). */
    if (age < 0 || age >= 0x28) return 0;

    /* vert_count = AGE * 2, clamp to 32, then subtract ramp past 24. */
    int vert_count = age * 2;
    if (vert_count > 0x20) vert_count = 0x20;
    if (age > 0x18) {
        vert_count += (0x18 - age) * 2;   /* negative add */
    }
    if (vert_count < 4) return 0;          /* engine `cmp 0x4 / jl skip` */

    /* pair_start (engine local_28) — 8 by default, then AGE-8 when
     * AGE>16, capped at 32. */
    int pair_start = 8;
    if (age > 0x10) {
        pair_start = age - 8;
        if (pair_start > 0x20) pair_start = 0x20;
    }

    /* AGE > 24 fade gate: gray -= (AGE - 24) * 16; skip if < 0. */
    int fade_gray = alpha_int_in;
    if (age > 0x18) {
        fade_gray -= (age - 0x18) * 0x10;
        if (fade_gray < 0) return 0;
    }

    if (out_vert_count) *out_vert_count = vert_count;
    if (out_pair_start) *out_pair_start = pair_start;
    if (out_fade_gray)  *out_fade_gray  = fade_gray;
    return 1;
}

void scene1_overlay_shape_7_scale_xy(const int32_t *slot,
                                     float alpha_mix,
                                     float *out_sx, float *out_sy)
{
    float blend_mix  = slot_get_f(slot, SCENE1_OVERLAY_OFF_BLEND_MIX);
    float scale_base = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_BASE);
    float scale_x    = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_X);

    /* Engine asm 0x415096..0x415129:
     *   base = scale_base * 0.01            (.rdata 0x5193a4)
     *   sx = ((1 - blend_mix) * base * alpha_mix * scale_x) / 0.5
     *   sy = (    blend_mix   * base * alpha_mix * scale_x) / 0.5
     * The engine calls scaling(sx, sy, sy) — sz aliased to sy.  */
    float base = scale_base * 0.01f;
    float sx = ((1.0f - blend_mix) * base * alpha_mix * scale_x) / 0.5f;
    float sy = (blend_mix * base * alpha_mix * scale_x) / 0.5f;
    if (out_sx) *out_sx = sx;
    if (out_sy) *out_sy = sy;
}

void scene1_overlay_shape_7_compose_world(float out[16],
                                          const int32_t *slot,
                                          float alpha_mix)
{
    float pos_x = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_X);
    float pos_y = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Y);
    float pos_z = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Z);
    float sx, sy;
    scene1_overlay_shape_7_scale_xy(slot, alpha_mix, &sx, &sy);

    /* Off-diagonal field mapping (same as shapes 3/6):
     *   slot[ROT_X] → RotationY   (asm 0x415167 call 0x4a3553)
     *   slot[ROT_Y] → RotationX   (asm 0x41517a call 0x4a35ef)
     *   slot[ROT_Z] → RotationZ   (asm 0x41518d call 0x4a368c — canonical)
     *
     * Engine multiply cascade (asm 0x4151b5..0x415236):
     *   S × RotY → (× RotZ) → (× RotX) → (× T)
     * Engine Multiply(out, A, B) = our mat4_mul(out, A, B) (= A * B).
     * mat4_mul is alias-safe; build incrementally via right-multiply. */
    float ry_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_X);
    float rx_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_Y);
    float rz_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_Z);

    float scratch[16];
    mat4_scaling(out, sx, sy, sy);                 /* S (sz aliased to sy) */
    mat4_rotation_y(scratch, ry_val);
    mat4_mul(out, out, scratch);                    /* S × RotY */
    mat4_rotation_z(scratch, rz_val);
    mat4_mul(out, out, scratch);                    /* (S×RotY) × RotZ */
    mat4_rotation_x(scratch, rx_val);
    mat4_mul(out, out, scratch);                    /* ((S×RotY)×RotZ) × RotX */
    mat4_translation(scratch, pos_x, pos_y, pos_z);
    mat4_mul(out, out, scratch);                    /* × T */
}

void scene1_overlay_shape_7_emit_strip(scene1_overlay_vertex *vbuf_window,
                                       int pair_count,
                                       const int32_t *shape_entry,
                                       float uv_origin_x, float uv_origin_y,
                                       int fade_gray)
{
    if (!vbuf_window || pair_count <= 0) return;

    float uv_size_x = 0.0f, uv_size_y = 0.0f;
    if (shape_entry) {
        uv_size_x = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]);
        uv_size_y = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]);
    }

    /* Engine asm 0x4152f1..0x4152fd:
     *   u_left  = (uv_origin_x + 0.5) / 256
     *   u_right = (uv_origin_x + uv_size_x - 0.5) / 256
     *   N       = pair_count = vert_count / 2 */
    float u_left  = (uv_origin_x + 0.5f) / 256.0f;
    float u_right = (uv_origin_x + uv_size_x - 0.5f) / 256.0f;
    float n_f     = (float)pair_count;

    uint32_t diffuse = scene1_overlay_diffuse_gray(fade_gray);

    /* Per-pair UV/diffuse writes — engine asm 0x415348..0x415392.
     * v varies linearly down the strip: v = (i * uv_size_y / N + base + 0.5)
     * / 256.  Both verts in a pair share v; vert A gets u_left, B gets
     * u_right.  */
    for (int i = 0; i < pair_count; i++) {
        float v = ((float)i * uv_size_y / n_f + uv_origin_y + 0.5f) / 256.0f;
        scene1_overlay_vertex *va = &vbuf_window[i * 2 + 0];
        scene1_overlay_vertex *vb = &vbuf_window[i * 2 + 1];
        va->u = u_left;   va->v = v;
        vb->u = u_right;  vb->v = v;
        va->diffuse = diffuse;
        vb->diffuse = diffuse;
    }
}

/* ---- Shapes 8/9/10: group strip emit (O.7) ------------------------ */

/* Static vbufs for shapes 8/9 (160 verts total) and shape 10 (160 verts
 * in 4 strips of 40).  Engine packs shape 8 at &DAT_00648e08 and shape 9
 * at &DAT_00649588 (+0x780 = 80 verts).  Shape 10 at &DAT_0064c508 with
 * 4 contiguous 40-vert strips.  Positions baked in by FUN_0040d132. */
scene1_overlay_vertex
    g_scene1_overlay_shape_89_vbuf[SCENE1_OVERLAY_SHAPE_89_VERT_COUNT * 2];
scene1_overlay_vertex
    g_scene1_overlay_shape_10_vbuf[SCENE1_OVERLAY_SHAPE_10_VERT_COUNT];

void scene1_overlay_shape_89_vbuf_init(void)
{
    /* Engine FUN_0040d132:
     *   Shape 8 (all.c L8286-8305):
     *     angle = i * 2π / 39  (i = 0..39)
     *     vert A: pos=(sin(angle)*128, 64, cos(angle)*128), v_placeholder=0.5019531
     *     vert B: pos=(sin(angle)*128,  0, cos(angle)*128), v_placeholder=0.7480469
     *     u_placeholder = (i*1.6153846 + 192.5) / 256  (overwritten per draw)
     *
     *   Shape 9 (all.c L8262-8284):
     *     angle = i * 2π / 39
     *     vert A: pos=(sin(angle)*128, 64, cos(angle)*128), v_placeholder=0.05078125
     *     vert B: pos=(sin(angle)*128*0.6, 0, cos(angle)*128*0.6), v_placeholder=0.24609375
     *     u_placeholder = (i*5.3333335 + 72.5) / 256
     *
     * The placeholder UVs / diffuse are overwritten in
     * scene1_overlay_shape_89_emit_strip per draw, but we initialise
     * them verbatim to mirror engine state. */
    for (int i = 0; i < SCENE1_OVERLAY_SHAPE_89_PAIR_COUNT; i++) {
        float angle = (float)i * TWO_PI_F / 39.0f;
        float sa = sinf(angle);
        float ca = cosf(angle);
        float xz_x = sa * 128.0f;
        float xz_z = ca * 128.0f;

        /* Shape 8 occupies slot 0 (verts 0..79). */
        {
            float u_pl = ((float)i * 1.6153846f + 192.5f) / 256.0f;
            scene1_overlay_vertex *va = &g_scene1_overlay_shape_89_vbuf[i * 2 + 0];
            scene1_overlay_vertex *vb = &g_scene1_overlay_shape_89_vbuf[i * 2 + 1];
            va->x = xz_x;  va->y = 64.0f;  va->z = xz_z;
            vb->x = xz_x;  vb->y =  0.0f;  vb->z = xz_z;
            va->u = u_pl;  va->v = 0.5019531f;
            vb->u = u_pl;  vb->v = 0.7480469f;
            va->diffuse = 0xFFFFFFFFu;
            vb->diffuse = 0xFFFFFFFFu;
        }

        /* Shape 9 occupies slot 1 (verts 80..159), vert B at 0.6× radius. */
        {
            float u_pl = ((float)i * 5.3333335f + 72.5f) / 256.0f;
            int base = SCENE1_OVERLAY_SHAPE_89_VERT_COUNT;
            scene1_overlay_vertex *va = &g_scene1_overlay_shape_89_vbuf[base + i * 2 + 0];
            scene1_overlay_vertex *vb = &g_scene1_overlay_shape_89_vbuf[base + i * 2 + 1];
            va->x = xz_x;          va->y = 64.0f;  va->z = xz_z;
            vb->x = xz_x * 0.6f;   vb->y =  0.0f;  vb->z = xz_z * 0.6f;
            va->u = u_pl;          va->v = 0.05078125f;
            vb->u = u_pl;          vb->v = 0.24609375f;
            va->diffuse = 0xFFFFFFFFu;
            vb->diffuse = 0xFFFFFFFFu;
        }
    }
}

void scene1_overlay_shape_10_vbuf_init(void)
{
    /* Engine FUN_0040d132 (all.c L8233-8261): 4 strips × 20 pairs.
     *   strip_angle = strip_idx * π/4 / 1  → reads as iStack_10 * π/4 / 4
     *                                       (verified: fVar3 * 1.5707964 / 4)
     *   inner_angle = i * 2π/19
     *
     * Wait — re-reading the asm: outer iter k has strip_angle = k * π/4 / 4
     * = k * π/16 for vert A's latitude, (k+1) * π/16 for vert B.  But the
     * outer loop runs 4 iters and strip_angle uses fVar3 = iStack_10 (the
     * loop iteration counter) directly.  Let me recompute via all.c lines
     * 8235-8260:
     *
     *   iStack_10 initial = 0;
     *   do {
     *     fVar3 = (float)iStack_10;                       // strip_idx
     *     iStack_10 += 1;
     *     do {  // inner
     *       dVar1 = (fVar3 * π/2) / 4;                    // strip_angle = k * π/8
     *       ...
     *       dVar2 = (iStack_10 * π/2) / 4;                // (k+1) * π/8
     *
     * So vert A is at strip_angle = strip_idx * π/8, vert B at (strip_idx+1)*π/8.
     * 4 strips cover [0, π/2] in latitude — north pole down to equator. */
    for (int k = 0; k < SCENE1_OVERLAY_SHAPE_10_STRIP_COUNT; k++) {
        float strip_a = (float)k * 1.5707964f / 4.0f;          /* k * π/8 */
        float strip_b = (float)(k + 1) * 1.5707964f / 4.0f;    /* (k+1) * π/8 */
        float sa = sinf(strip_a), ca = cosf(strip_a);
        float sb = sinf(strip_b), cb = cosf(strip_b);

        scene1_overlay_vertex *strip_base =
            &g_scene1_overlay_shape_10_vbuf[k * SCENE1_OVERLAY_SHAPE_10_VERTS_PER_STRIP];
        for (int i = 0; i < SCENE1_OVERLAY_SHAPE_10_PAIRS_PER_STRIP; i++) {
            float inner = (float)i * TWO_PI_F / 19.0f;
            float si = sinf(inner), ci = cosf(inner);

            scene1_overlay_vertex *va = &strip_base[i * 2 + 0];
            scene1_overlay_vertex *vb = &strip_base[i * 2 + 1];

            /* vert A: lat = strip_a; vert B: lat = strip_b.
             *   x = sin(inner) * sin(lat) * 128
             *   y = cos(lat) * 128
             *   z = cos(inner) * sin(lat) * 128 */
            va->x = si * sa * 128.0f;
            va->y = ca * 128.0f;
            va->z = ci * sa * 128.0f;
            vb->x = si * sb * 128.0f;
            vb->y = cb * 128.0f;
            vb->z = ci * sb * 128.0f;

            /* Placeholder diffuse + UV (overwritten per draw). */
            va->diffuse = 0xFFFFFFFFu;
            vb->diffuse = 0xFFFFFFFFu;
            va->u = 0.0f;  va->v = 0.0f;
            vb->u = 0.0f;  vb->v = 0.0f;
        }
    }
}

void scene1_overlay_shape_89_10_scale(const int32_t *slot,
                                      float alpha_mix,
                                      float *out_s_h, float *out_s_v)
{
    float blend_mix     = slot_get_f(slot, SCENE1_OVERLAY_OFF_BLEND_MIX);
    float scale_base    = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_BASE);
    float scale_x       = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_X);
    float scale_y_ratio = slot_get_f(slot, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO);

    /* Engine asm 0x415b20..0x415b44:
     *   s_h = ((1 - blend_mix) * scale_base * alpha_mix * scale_x * 0.588) / 0.5 * 0.02
     * .rdata: 0x51993c=0.588, 0x51935c=0.5, 0x5198dc=0.02 */
    float s_h = ((1.0f - blend_mix) * scale_base * alpha_mix * scale_x
                 * 0.588f) / 0.5f * 0.02f;

    /* Engine asm 0x415b47..0x415b6e:
     *   s_v = (blend_mix * scale_base * alpha_mix * scale_x * 1.26) / 0.5
     *           * scale_y_ratio / 0.5 * 0.015
     * .rdata: 0x519938=1.26, 0x519940=0.015 */
    float s_v = (blend_mix * scale_base * alpha_mix * scale_x * 1.26f) / 0.5f
                * scale_y_ratio / 0.5f * 0.015f;

    if (out_s_h) *out_s_h = s_h;
    if (out_s_v) *out_s_v = s_v;
}

void scene1_overlay_shape_89_10_compose_world(float out[16],
                                              const int32_t *slot,
                                              float alpha_mix)
{
    float pos_x = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_X);
    float pos_y = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Y);
    float pos_z = slot_get_f(slot, SCENE1_OVERLAY_OFF_POS_Z);
    float s_h, s_v;
    scene1_overlay_shape_89_10_scale(slot, alpha_mix, &s_h, &s_v);

    /* Off-diagonal field mapping: slot[ROT_Y] (Ghidra's "rot.y") feeds
     * RotationX (engine asm 0x415b8e calls 0x4a35ef = mat4_rotation_x
     * short-jmp).  Same convention as shapes 3/6/7. */
    float rx_val = slot_get_f(slot, SCENE1_OVERLAY_OFF_ROT_Y);

    /* Engine multiply cascade (asm 0x415ba9..0x415be2, Multiply(out, A, B)
     * with B aliased to out → out = A × out):
     *   out = T
     *   out = RotX × T
     *   out = Scale × (RotX × T) = S × RotX × T */
    float scratch[16];
    mat4_translation(out, pos_x, pos_y, pos_z);
    mat4_rotation_x(scratch, rx_val);
    mat4_mul(out, scratch, out);                  /* RotX × T */
    mat4_scaling(scratch, s_h, s_v, s_h);         /* sz aliased to sx */
    mat4_mul(out, scratch, out);                  /* S × RotX × T */
}

void scene1_overlay_shape_89_emit_strip(scene1_overlay_vertex *vbuf,
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

    /* Engine asm 0x415d97..0x415da6:
     *   u_left  = (uv_origin_x + 0.5) / 256
     *   u_right = (uv_origin_x + uv_size_x - 0.5) / 256
     * v_step = (uv_size_y - 1) / 39  (.rdata 0x5194cc = 39.0) */
    float u_left  = (uv_origin_x + 0.5f) / 256.0f;
    float u_right = (uv_origin_x + uv_size_x - 0.5f) / 256.0f;
    float v_step  = (uv_size_y - 1.0f) / 39.0f;

    uint32_t diffuse = scene1_overlay_diffuse_gray(alpha_int);

    /* Per-pair UV writes — engine asm 0x415dcc..0x415e07 (40 iter, +0x30
     * stride per pair).  Vert A gets u_left, vert B gets u_right, both
     * share v.  Positions remain pre-baked. */
    for (int i = 0; i < SCENE1_OVERLAY_SHAPE_89_PAIR_COUNT; i++) {
        float v = ((float)i * v_step + uv_origin_y + 0.5f) / 256.0f;
        scene1_overlay_vertex *va = &vbuf[i * 2 + 0];
        scene1_overlay_vertex *vb = &vbuf[i * 2 + 1];
        va->u = u_left;   va->v = v;
        vb->u = u_right;  vb->v = v;
        va->diffuse = diffuse;
        vb->diffuse = diffuse;
    }
}

void scene1_overlay_shape_10_emit_strip(scene1_overlay_vertex *strip_vbuf,
                                        int strip_idx,
                                        const int32_t *shape_entry,
                                        float uv_origin_x, float uv_origin_y,
                                        int alpha_int)
{
    if (!strip_vbuf) return;

    float uv_size_x = 0.0f, uv_size_y = 0.0f;
    if (shape_entry) {
        uv_size_x = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]);
        uv_size_y = bits_to_f(shape_entry[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]);
    }

    /* Engine asm 0x415c68..0x415c8e:
     *   u_step = (uv_size_x - 1) / 4         (.rdata 0x51939c = 4.0)
     *   v_step = (uv_size_y - 1) / 19        (.rdata 0x5194c0 = 19.0)
     *   u_left  = (strip_idx     * u_step + uv_origin_x + 0.5) / 256
     *   u_right = ((strip_idx+1) * u_step + (uv_origin_x - 0.5)) / 256
     * Note u_right uses (origin - 0.5) bias (NOT +uv_size_x - 0.5 like
     * shapes 8/9) — engine asm 0x415c52 stashes local_2c = uv_origin_x
     * - 0.5 then 415cbe uses it for u_right. */
    float u_step  = (uv_size_x - 1.0f) / 4.0f;
    float v_step  = (uv_size_y - 1.0f) / 19.0f;
    float u_left  = ((float)strip_idx       * u_step + uv_origin_x + 0.5f) / 256.0f;
    float u_right = ((float)(strip_idx + 1) * u_step + (uv_origin_x - 0.5f)) / 256.0f;

    uint32_t diffuse = scene1_overlay_diffuse_gray(alpha_int);

    /* Per-pair UV writes — engine asm 0x415ccd..0x415d0c (20 iter, +0x30
     * stride per pair). */
    for (int i = 0; i < SCENE1_OVERLAY_SHAPE_10_PAIRS_PER_STRIP; i++) {
        float v = ((float)i * v_step + uv_origin_y + 0.5f) / 256.0f;
        scene1_overlay_vertex *va = &strip_vbuf[i * 2 + 0];
        scene1_overlay_vertex *vb = &strip_vbuf[i * 2 + 1];
        va->u = u_left;   va->v = v;
        vb->u = u_right;  vb->v = v;
        va->diffuse = diffuse;
        vb->diffuse = diffuse;
    }
}

/* ═══ HUD camera + projection setup (O.11, FUN_00452f58) ════════════════
 *
 * Pure-C matrix builder for the 2D-overlay HUD camera.  Raw asm
 * 0x452fca..0x4530f0 confirms the chain:
 *
 *   pre_matrix = mat4_mul(Y, X)
 *   where Y = mat4_rotation_y(π/2 - atan2(dy, hyp))
 *         X = mat4_rotation_x(atan2(0, lookat_z) + π)
 *
 * The engine literally passes the precomputed constant 302500.0 (= 550²)
 * to sqrt — the compiler folded the (0,0,-550)/(0,0,0) state.  Port
 * derives hyp at runtime as the 2D horizontal distance from eye to
 * lookat so arbitrary callers compute correctly; the engine's hard-
 * coded state still produces hyp = 550.
 *
 * Singular path (hyp == 0 → engine FUN_00404bb8) is unported; port
 * collapses to rot_y_angle = 0 (= identity matrix on Y).
 */
void scene1_overlay_setup_compute(const float eye[3],
                                  const float lookat[3],
                                  float view[16],
                                  float proj[16],
                                  float pre_matrix[16])
{
    if (!eye || !lookat) return;

    /* Engine constants from .rdata (verified via tools/analyze/pe.py):
     *   0x519394 = π/4         (fov_y)
     *   0x519338 = 4/3         (aspect)
     *   0x5194f0 = 10.0        (near)
     *   0x519d40 = 20000.0     (far)
     *   0x519434 = π/2
     *   0x51943c = π
     */
    const float kFovY    = 0.7853981852531433f;
    const float kAspect  = 1.3333333730697632f;
    const float kNear    = 10.0f;
    const float kFar     = 20000.0f;
    const float kPiOver2 = 1.5707963705062866f;
    const float kPi      = 3.1415927410125732f;

    /* RotationX angle = atan2(0, eye_z) + π.  The engine's folded
     * constant −550 (FUN_00503dd0(0, −550.0)) is the z of DAT_06a47120
     * — the EYE triplet (§21.31.4 role fix; the lookat DAT_06a475f0 is
     * the zero vector, so reading lookat_z here would flip the fold to
     * atan2(0,0) and change the PHC #16-verified pre-matrix). */
    float rot_x_angle = atan2f(0.0f, eye[2]) + kPi;

    /* sqrt of squared horizontal distance — engine hard-codes 302500
     * for its (0,0,-550) state; we recover the formula from the literal
     * eye/lookat inputs. */
    float dx_xz = lookat[0] - eye[0];
    float dz_xz = lookat[2] - eye[2];
    float hyp   = sqrtf(dx_xz * dx_xz + dz_xz * dz_xz);

    float rot_y_angle;
    if (hyp == 0.0f) {
        rot_y_angle = 0.0f;
    } else {
        /* Engine local_c = DAT_06a47124 − DAT_06a475f4 = eye.y − at.y,
         * then FUN_00503dd0(hyp, dy) = atan2(y=hyp, x=dy) — note the
         * arg order: HYP is the y argument.  Engine state (dy=0,
         * hyp=550) → atan2(550, 0) = π/2 → rot_y_angle = 0 → the pre
         * matrix is IDENTITY (capture-proven §21.31.4: retail coin-
         * shower worlds are pure T×S — the old swapped order produced
         * RotY(π/2), turning every shape-0/5 HUD overlay quad edge-on
         * = the invisible-sliver half of the coin-shower bug). */
        float dy = eye[1] - lookat[1];
        rot_y_angle = kPiOver2 - atan2f(hyp, dy);
    }

    float rot_y[16], rot_x[16];
    mat4_rotation_y(rot_y, rot_y_angle);
    mat4_rotation_x(rot_x, rot_x_angle);

    if (pre_matrix) {
        mat4_mul(pre_matrix, rot_y, rot_x);
    }

    if (view) {
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        mat4_lookat_rh(view, eye, lookat, up);
    }

    if (proj) {
        mat4_perspective_fov_rh(proj, kFovY, kAspect, kNear, kFar);
    }
}
