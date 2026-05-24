/*
 * scene1_shop_walker_helpers.c — D3D-free helpers for scene1_shop_walker.c.
 *
 * Split out so host unit tests can link the algebraic per-record helpers
 * without dragging in <d3d8.h>.  scene1_shop_walker.c itself stays
 * `#ifdef _WIN32` for the actual walker entry (SetTransform + emit calls).
 *
 * Today this file holds the Pass D record-emit predicate + matrix
 * composer; future per-pass body ports (A/B/C/E/G) will add siblings.
 */

#include "scene1_shop_walker.h"

#include <math.h>

#include "math3d.h"
#include "scene1_records.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

/* Engine asm @ 0x455bdc..0x455bf6 — cascading cmp/je on `int [edi]`.
 * The -1 sentinel short-circuit is asm-order first (cmp eax,0xffffffff
 * before the type-pair checks). */
int sw_pass_d_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_A_OFF_TYPE];
    if (type == -1) return 0;
    return type == 0x74 || type == 0x79 || type == 0x96;
}

/* Engine asm @ 0x455bfc..0x455cb1 — six float-loads + 3 D3DX-style
 * matrix calls (Translation, two Scaling, RotationX, three Multiplies).
 *
 *   scale  = slot[SCALE_float] * 0.2f            // .rdata 0x5198d8 = 0.2f
 *   M      = Translation(pos)
 *   S_neg  = Scaling(-scale, scale, scale)       // X mirror
 *   M      = S_neg * M     // Multiply(M, S_neg, M) — left-mul (C8h.4d)
 *   Rx     = RotationX(rotX)
 *   M      = Rx * M
 *   I      = Scaling(1, 1, 1)
 *   M      = I * M         // no-op, dropped for clarity
 *
 * Field offsets from asm: edi anchors at TYPE (slot+0x30); edi-0x30/-0x2c/
 * -0x28 = POS_X/Y/Z, edi-0x18 = ROT_X, edi+0x8 = SCALE.  All `fld dword`
 * (float-loads) — Ghidra's `(float)piVar8[2]` cast in the decomp was a
 * decompiler quirk, not an int→float convert. */
void sw_pass_d_compose_world(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_A_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_A_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_A_OFF_POS_Z];
    float rot_x = *(const float *)&slot[SCENE1_RECORDS_A_OFF_ROT_X];
    float scale = *(const float *)&slot[SCENE1_RECORDS_A_OFF_SCALE] * 0.2f;

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, -scale, scale, scale);
    mat4_mul(out, scratch, out);

    mat4_rotation_x(scratch, rot_x);
    mat4_mul(out, scratch, out);
}

/* ─── Pass D mesh slot (C8e.bridge) ──────────────────────────────────────
 *
 * Module-static stand-in for the engine's `&DAT_073a9680` (train_iwa.x,
 * populated by FUN_00474a9a's DUNGEON branch only — see the chip notes
 * inline at sw_pass_d).  Lives in the helpers TU so host tests can
 * exercise the setter/getter contract without linking <d3d8.h>.
 */

static const mesh_t *g_pass_d_mesh = NULL;

void scene1_shop_walker_set_pass_d_mesh(const mesh_t *m)
{
    g_pass_d_mesh = m;
}

const mesh_t *scene1_shop_walker_get_pass_d_mesh(void)
{
    return g_pass_d_mesh;
}

/* ─── Pass D unlit-debug override (C8e.smoke) ────────────────────────────
 *
 * Same TU as the mesh setter for the same reason — host tests link this
 * without dragging in <d3d8.h>.  sw_pass_d (Win32-only) reads the flag
 * via the getter on every walker entry. */

static int g_debug_pass_d_unlit = 0;

void scene1_shop_walker_set_debug_pass_d_unlit(int on)
{
    g_debug_pass_d_unlit = on ? 1 : 0;
}

int scene1_shop_walker_get_debug_pass_d_unlit(void)
{
    return g_debug_pass_d_unlit;
}

/* ─── Pass B helpers (C8c.B) ─────────────────────────────────────────────
 *
 * Engine FUN_004552d0 L97-L193 / asm @ 0x4555e0..0x4559cf.  Outer
 * iteration over g_scene1_records_b (stride 0x49 dw, bounded by
 * g_scene1_records_b_count).  Three sub-bodies dispatched by TYPE:
 *
 *   TYPE == 0x8c — primary body.  Gated by PART_IDX % 2 == 0
 *     (asm `idiv 2; cmp edx, 1; jge skip` at 0x45560f → skip when
 *     remainder >= 1, emit when 0).  Composes
 *       MATRIX0 × Rx(ROT_X) × S(-s,s,s) × T(POS)
 *     where s = LIFE_MULT * 0.06f (.rdata 0x519d6c).  Emits via
 *     FUN_00455191(&DAT_073a96a8).
 *
 *   TYPE == 0x9b or 0x9c — outer body (no gate).  Composes
 *       Ry(ROT_SCR) × Rx(-ROT_X) × S(-s,s,s) × T(POS)
 *     where s = LIFE_MULT * 0.05f (.rdata 0x5198f8).  Emits via
 *     FUN_00455191(&DAT_073a96f8).  Then a 4-iter spoke loop emits
 *     via FUN_00455191(&DAT_073a9720), per-spoke matrix:
 *       T(sinf(θ)·r/0.05, cosf(θ)·r/0.05, 70.0f) × outer
 *
 *   Spoke (θ, r) per-spoke computation:
 *     base_angle = spoke_idx * π/2
 *     radius = 0.1f (default; .rdata 0x5193a0)
 *     if TYPE == 0x9b and AGE > 60:  extra=AGE-60; r=extra*0.1; θ+=extra*0.2
 *     elif TYPE == 0x9c and AGE > 20: extra=AGE-20; r=extra*0.1; θ+=extra*0.2
 *     if r > 2.5f: r = 2.5f  (.rdata 0x5198d0; fcomp + jbe pattern)
 *
 *   Source comments in scene1_shop_walker.h L24-L31 had stale type
 *   values (claimed "raw 0xf7 / 0xf8" for the secondary branch); asm
 *   `cmp eax, 0x9b` / `cmp eax, 0x9c` at 0x455744/0x45574b is
 *   authoritative — Ghidra's float-as-int reinterpretation produced
 *   2.17/2.18e-43 which by `N * 2^-149` is 0x9b/0x9c, not 0xf7/0xf8.
 *
 * All three mesh-record slots (0x73a96a8 / f8 / 0x73a9720) are
 * DUNGEON-loaded by an engine static-init path not yet ported (same
 * shape as Pass D's &DAT_073a9680).  HOUSE leaves them BSS-zero so
 * the emit calls short-circuit inside scene1_emit_record.  Pass B
 * types {0x8c, 0x9b, 0x9c} are also NOT in any landed C8j
 * allocator's type set, so Pass B is doubly dormant in HOUSE (no
 * records, no meshes). */

int sw_pass_b_should_emit_main(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    if (type != 0x8c) return 0;
    int32_t part_idx = slot[SCENE1_RECORDS_B_OFF_PART_IDX];
    /* Engine asm @ 0x45560f: signed idiv by 2; reject when edx >= 1.
     * For non-negative part_idx this is the standard "odd → skip"
     * gate; negative parities behave per signed-idiv convention. */
    int rem = part_idx % 2;
    return rem < 1 ? 1 : 0;
}

int sw_pass_b_should_emit_outer(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    return (type == 0x9b || type == 0x9c) ? 1 : 0;
}

void sw_pass_b_compose_world_main(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float rot_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_ROT_X];
    float life_mult = *(const float *)&slot[SCENE1_RECORDS_B_OFF_LIFE_MULT];
    float scale = life_mult * 0.06f;
    const float *matrix0 = (const float *)&slot[SCENE1_RECORDS_B_OFF_MATRIX0];

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, -scale, scale, scale);
    mat4_mul(out, scratch, out);   /* out = S × T */

    mat4_rotation_x(scratch, rot_x);
    mat4_mul(out, scratch, out);   /* out = Rx × S × T */

    mat4_mul(out, matrix0, out);   /* out = MATRIX0 × Rx × S × T */
    /* Engine then does Multiply(out, Scaling(1,1,1), out) — algebraic
     * no-op, dropped here. */
}

void sw_pass_b_compose_world_outer(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float rot_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_ROT_X];
    float rot_scr = *(const float *)&slot[SCENE1_RECORDS_B_OFF_ROT_SCR];
    float life_mult = *(const float *)&slot[SCENE1_RECORDS_B_OFF_LIFE_MULT];
    float scale = life_mult * 0.05f;

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, -scale, scale, scale);
    mat4_mul(out, scratch, out);   /* out = S × T */

    mat4_rotation_x(scratch, -rot_x);
    mat4_mul(out, scratch, out);   /* out = Rx × S × T */

    mat4_rotation_y(scratch, rot_scr);
    mat4_mul(out, scratch, out);   /* out = Ry × Rx × S × T */
}

void sw_pass_b_spoke_pose(float *out_radius, float *out_angle,
                          const int32_t *slot, int spoke_idx)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    int32_t age  = slot[SCENE1_RECORDS_B_OFF_AGE];  /* int per engine asm @ 0x45584c */

    float radius = 0.1f;
    float angle  = (float)spoke_idx * (float)M_PI_2;

    if (type == 0x9b) {
        if (age > 60) {
            int extra = age - 60;
            radius = (float)extra * 0.1f;
            angle += (float)extra * 0.2f;
        }
    } else if (age > 20) {
        /* engine: the `else` arm is the 0x9c branch (also covers any
         * other type, but the outer body only reaches here for
         * 0x9b/0x9c, so the asymmetric 0x9c threshold is exclusively
         * for that type). */
        int extra = age - 20;
        radius = (float)extra * 0.1f;
        angle += (float)extra * 0.2f;
    }

    /* fcomp ds:0x5198d0 (=2.5); jbe past clamp → clamp only when r > 2.5. */
    if (radius > 2.5f) radius = 2.5f;

    *out_radius = radius;
    *out_angle  = angle;
}

void sw_pass_b_compose_world_spoke(float out[16], const float outer[16],
                                   const int32_t *slot, int spoke_idx)
{
    float radius, angle;
    sw_pass_b_spoke_pose(&radius, &angle, slot, spoke_idx);

    /* Engine: T(sin(angle) * radius / 0.05f, cos(angle) * radius /
     * 0.05f, 70.0f).  The /0.05 is equivalent to *20. */
    float tx = sinf(angle) * radius / 0.05f;
    float ty = cosf(angle) * radius / 0.05f;
    float tz = 70.0f;

    float t_spoke[16];
    mat4_translation(t_spoke, tx, ty, tz);

    /* Engine asm @ 0x45596d: Multiply(out=local_f0, a=local_1b4 (t_spoke),
     * b=local_68 (outer)) → out = t_spoke × outer.  Memcpy local_f0 →
     * local_1b4 + SetTransform follows. */
    mat4_mul(out, t_spoke, outer);
}

/* ─── Pass C helpers (C8c.C) ─────────────────────────────────────────────
 *
 * Engine FUN_004552d0 L198-L237 / asm @ 0x455a35..0x455bab.  Walks
 * g_scene1_records_b at slot[0] base (DAT_069324b0 in engine), stride
 * 0x49 dw, count-bounded by g_scene1_records_b_count.  Single body
 * with a multi-case TYPE filter:
 *
 *   TYPE filter cascade @ 0x455a42..0x455a7e (asm):
 *     0x23, 0x2c, 0x2b → emit iff PART_IDX % 2 == 0 (signed idiv 2;
 *                         `cmp edx, esi (=1); jge skip` ⇒ skip when rem >= 1).
 *     0x56, 0x96       → always emit (jumps directly past parity gate)
 *     other            → skip
 *
 *   Source-comment correction: scene1_shop_walker.c L237-L242 named
 *   types {0x37, 0x44, 0x55, 0x95, 0x88}; asm `cmp eax, K` is
 *   authoritative — actual types are {0x23, 0x2c, 0x56, 0x96, 0x2b}.
 *   Decomp's float-as-int reinterp (4.90/6.16/1.20/2.10/6.02e-44/43)
 *   maps via `N * 2^-149` to 0x23/0x2c/0x56/0x96/0x2b respectively.
 *
 *   Matrix chain:
 *     scale = LIFE_MULT * 0.2f          (.rdata 0x5198d8)
 *     T  = Translation(POS)
 *     S  = Scaling(-scale, scale, scale)
 *     T  = S × T                         (Multiply pattern same as Pass B)
 *     Ry = RotationY(ROT_SCR)            (slot[35]; engine asm `fld [esi+0x8c]`
 *                                          + `call 0x4a3553` = RotY short jmp)
 *     T  = Ry × S × T
 *     T  = MATRIX0 × Ry × S × T          (slot[50..65], 16-float matrix)
 *     T  = I × T                          (dead Scaling(1,1,1) + Multiply, dropped)
 *
 *   Emits via FUN_00455191(&DAT_073a9680) — the SAME mesh-record slot
 *   as Pass D (train_iwa.x, DUNGEON-loaded only).  In HOUSE this is
 *   NULL → emit short-circuits.  Pass C IS smoke-fireable on
 *   table-B types that ARE in landed C8j allocator sets:
 *     0x23 (entity matrix-init), 0x56 (NPC matrix-init), 0x96 (NPC
 *     player-aim), 0x2b (NPC owner+0x420 family).  Only 0x2c is
 *     un-allocated by any landed populator today.
 *
 *   Differs from Pass B 0x8c which uses Rx (not Ry) and scale 0.06
 *   (not 0.2).  Same scale factor as Pass D (0.2) but Pass D's
 *   scale field is slot[SCALE], not LIFE_MULT. */

int sw_pass_c_should_emit(const int32_t *slot)
{
    int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
    if (type == 0) return 0;  /* engine: `cmp eax, ebx (=0); je skip` */

    /* Types that always emit. */
    if (type == 0x56 || type == 0x96) return 1;

    /* Types that require PART_IDX even. */
    if (type == 0x23 || type == 0x2c || type == 0x2b) {
        int32_t part_idx = slot[SCENE1_RECORDS_B_OFF_PART_IDX];
        int rem = part_idx % 2;
        return rem < 1 ? 1 : 0;
    }

    return 0;
}

void sw_pass_c_compose_world(float out[16], const int32_t *slot)
{
    float pos_x = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_X];
    float pos_y = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Y];
    float pos_z = *(const float *)&slot[SCENE1_RECORDS_B_OFF_POS_Z];
    float rot_scr = *(const float *)&slot[SCENE1_RECORDS_B_OFF_ROT_SCR];
    float life_mult = *(const float *)&slot[SCENE1_RECORDS_B_OFF_LIFE_MULT];
    float scale = life_mult * 0.2f;
    const float *matrix0 = (const float *)&slot[SCENE1_RECORDS_B_OFF_MATRIX0];

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, -scale, scale, scale);
    mat4_mul(out, scratch, out);   /* out = S × T */

    mat4_rotation_y(scratch, rot_scr);
    mat4_mul(out, scratch, out);   /* out = Ry × S × T */

    mat4_mul(out, matrix0, out);   /* out = MATRIX0 × Ry × S × T */
    /* Engine then does Multiply(out, Scaling(1,1,1), out) — algebraic
     * no-op, dropped here. */
}
