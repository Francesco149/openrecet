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
#include <string.h>

#include "math3d.h"
#include "rng.h"                 /* rng_next15 / rng_next_unit (LCG — RNG parity) */
#include "scene1_bg_npc.h"       /* scene1_bg_npc_type_to_char (DAT_005c7ce0[idx*2]) */
#include "scene1_chr_sprite.h"   /* chr_anim_tick (FUN_00482a71) — RNG-neutral step */
#include "scene1_records.h"
#include "scene1_shop_display.h" /* shop_display_grid_cell (DAT_074b28e8 walk grid) */
#include "scene1_player_ctrl.h"  /* player_ctrl_facing_octant (b850 angle→octant) */
#include "scene1_particles_tick.h"/* g_scene1_camera_yaw (_DAT_073de39c) */

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

/* ─── Pass A helpers (C8c.A) ─────────────────────────────────────────────
 *
 * Engine FUN_004552d0 L68-L96 / asm @ 0x45548b..0x4555b6.  Iterates the
 * fixed range DAT_0076bd94..DAT_007c8f94 (= 0x5D200 bytes / stride 0xba4
 * bytes = 128 records — per-stage furniture/NPC instance table; not
 * ported as a typed global yet).
 *
 * Per-record gate cascade (asm @ 0x455490..0x4554c6):
 *   slot[1] != 0       (active)
 *   slot[0x1b4] < 1    (visibility; asm `jg` = signed > 0 skip)
 *   slot[0] ∈ {0x3e, 0x3f, 0x41, 0x42}   (type filter)
 *   slot[0x178] != -1  (sub-gate)
 *
 * Variant selector (asm @ 0x4554cc..0x4554ec): TYPE ∈ {0x3f, 0x42} →
 * variant 1; {0x3e, 0x41} → variant 0.  Indexes two adjacent
 * (pos_x, pos_y, pos_z) triplets at slot[0xc5..0xc7] and slot[0xc8..0xca].
 *
 * Matrix chain (asm @ 0x4554ec..0x455598):
 *   T   = Translation(pos_v)             // thunk 0x4a34b0 (= Translation)
 *   S   = Scaling(-0.04, 0.04, 0.04)     // thunk 0x4a3400 (= Scaling);
 *                                         //   .rdata 0x5198c4 = 0.04,
 *                                         //   .rdata 0x519d70 = -0.04
 *   T   = S × T                           // Multiply 2-arg form (asm 0x4a2a10);
 *                                         //   Ghidra dropped 3rd arg =
 *                                         //   `Multiply(T, S, T)` ⇒
 *                                         //   T = S × T (left-mul)
 *   ang = (float)slot[-0x23] * 0.05f     // fild (int→float) then fmul
 *                                         //   .rdata 0x5198f8 = 0.05
 *   Rx  = RotationX(ang)                 // thunk 0x4a35ef (= RotationX,
 *                                         //   verified via Pass B's outer
 *                                         //   asm at 0x4557d2 paired with
 *                                         //   `Rx(-rot_x)` in helpers)
 *   T   = Rx × (S × T)                   // Multiply 3-arg
 *
 * Emits via FUN_00455191(0) — null mesh-record arg; engine reads the
 * default Pass A mesh from a static slot we haven't identified.  HOUSE
 * leaves the slot NULL so the emit short-circuits inside scene1_emit_record
 * (mesh-NULL fast path).
 *
 * Doubly dormant in HOUSE: (a) the underlying table isn't ported as
 * typed storage so the iteration uses a count-stub returning 0, and
 * (b) even if records populate, the active flag is BSS-zero by engine
 * design.  Helpers landed for testability and to verify the matrix
 * chain against the asm; iteration wires through when the table ports.
 *
 * Note: ROT_SRC is read as `fild` (load-int + convert), unlike Pass
 * B/C/D's `fld` (float-load) for SCALE / LIFE_MULT.  Engine likely
 * stores ROT_SRC as a frame counter that's integer-incremented per
 * tick.  This matters when constructing synthetic slots in tests —
 * write the int directly, don't memcpy a float bit pattern. */

int sw_pass_a_should_emit(const int32_t *slot)
{
    if (slot[SCENE1_RECORDS_SHOP_OFF_ACTIVE] == 0) return 0;
    /* engine asm `jg` is signed: skip when [0x1b4] > 0 → admit when <= 0,
     * i.e. < 1 for integers. */
    if (slot[SCENE1_RECORDS_SHOP_OFF_VISIBILITY] > 0) return 0;

    int32_t type = slot[SCENE1_RECORDS_SHOP_OFF_TYPE];
    if (type != 0x3e && type != 0x3f && type != 0x41 && type != 0x42)
        return 0;

    if (slot[SCENE1_RECORDS_SHOP_OFF_SUBGATE] == -1) return 0;
    return 1;
}

int sw_pass_a_variant(int32_t type)
{
    if (type == 0x3f || type == 0x42) return 1;
    return 0;
}

void sw_pass_a_compose_world(float out[16], const int32_t *slot)
{
    int32_t type    = slot[SCENE1_RECORDS_SHOP_OFF_TYPE];
    int     variant = sw_pass_a_variant(type);

    /* Variant 0 reads slot[0xc5..0xc7]; variant 1 reads slot[0xc8..0xca].
     * Engine asm: `lea ecx, [eax+eax*2]` after `mov eax, [ebp-0xc]`
     * builds ecx = variant*3, then `lea` with +0x31c/0x318/0x314
     * resolves to the per-axis dword. */
    int pos_base = SCENE1_RECORDS_SHOP_OFF_POS_X_V0 + variant * 3;
    float pos_x = *(const float *)&slot[pos_base];
    float pos_y = *(const float *)&slot[pos_base + 1];
    float pos_z = *(const float *)&slot[pos_base + 2];

    int32_t rot_src = slot[SCENE1_RECORDS_SHOP_OFF_ROT_SRC];
    float   rot_angle = (float)rot_src * 0.05f;

    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, -0.04f, 0.04f, 0.04f);
    mat4_mul(out, scratch, out);    /* out = S × T */

    mat4_rotation_x(scratch, rot_angle);
    mat4_mul(out, scratch, out);    /* out = Rx × S × T */
}

/* ─── Pass F helpers (C8c.F) ─────────────────────────────────────────────
 *
 * Engine FUN_004552d0 L318-L324 / asm @ 0x455ee4..0x455f1d.  Walks the
 * same DAT_0076bd94..DAT_007c8f94 range as Pass A (128 records × stride
 * 0x2e9 dw).  Per-record body is just a gate cascade + a delegation
 * to a 526-byte scene-tree dispatcher (FUN_00456d48, not yet ported).
 *
 * Gate cascade (asm @ 0x455ee9..0x455f02):
 *   slot[1] != 0            (ACTIVE — same field as Pass A)
 *   (&DAT_005c2410)[type * 0x68 bytes] == 1
 *                            (TYPE-enable table lookup; each per-type
 *                             record is 0x1a dwords = 0x68 bytes, first
 *                             dword is the enable flag)
 *   slot[-0x12] == 0xff      (STATUS_F field; signed int dword compared
 *                             to value 255, asm `cmp DWORD PTR
 *                             [edi-0x48], 0xff`)
 *
 * Action: FUN_00456d48(slot - 0x109).  The arg is the scene-tree
 * record pointer (offset -0x109 dw = -0x424 bytes from the slot
 * anchor; engine asm `lea eax, [edi-0x424]`).  Engine doesn't write
 * SetTransform itself — that's done inside FUN_00456d48 with a
 * Translation × Scaling × RotationY chain followed by FUN_00404a20
 * (the scene-tree walker).
 *
 * Dormant in HOUSE: (a) the DAT_0076bd94 table is unported (count_stub
 * returns 0), and (b) the DAT_005c2410 type-enable table is BSS-zero
 * at boot — no in-binary writers found in our decomp.  The default
 * type_enabled hook returns 0 to match BSS-zero engine state. */

static int sw_pass_f_type_enabled_default(int32_t type)
{
    (void)type;
    /* DAT_005c2410 is BSS-zero at boot; HOUSE never populates it. */
    return 0;
}

static int (*g_pass_f_type_enabled_hook)(int32_t) =
    sw_pass_f_type_enabled_default;

void sw_pass_f_set_type_enabled_hook(int (*hook)(int32_t type))
{
    g_pass_f_type_enabled_hook = hook ? hook : sw_pass_f_type_enabled_default;
}

int sw_pass_f_type_enabled(int32_t type)
{
    return g_pass_f_type_enabled_hook(type);
}

int sw_pass_f_should_emit(const int32_t *slot, int type_enabled)
{
    if (slot[SCENE1_RECORDS_SHOP_OFF_ACTIVE] == 0) return 0;
    if (!type_enabled) return 0;
    /* Engine: signed int compare against 0xff = 255. */
    if (slot[SCENE1_RECORDS_SHOP_OFF_STATUS_F] != 0xff) return 0;
    return 1;
}

static void (*g_pass_f_emit_hook)(const int32_t *) = NULL;

void sw_pass_f_set_emit_hook(void (*hook)(const int32_t *record_offset))
{
    g_pass_f_emit_hook = hook;
}

void sw_pass_f_clear_emit_hook(void)
{
    g_pass_f_emit_hook = NULL;
}

/* Internal — invoked by sw_pass_f when an emit fires.  Lives here so
 * tests can link without dragging in <d3d8.h>. */
void sw_pass_f_fire_emit(const int32_t *record_offset)
{
    if (g_pass_f_emit_hook) g_pass_f_emit_hook(record_offset);
}

/* ════════════════════════════════════════════════════════════════════════════
 *  in-shop browsing-customer chibi NPCs
 *  (FUN_0046f8ba / FUN_0046f914 / FUN_0046fbb7 / FUN_0046fa31 / FUN_0046fbee /
 *   FUN_0047019f — the cc08==4 wandering crowd; logic + RNG only)
 * ════════════════════════════════════════════════════════════════════════════
 *
 * D3D-free so host tests can assert the exact LCG-draw count per spawn/tick.
 * The slot byte/dword offsets are cross-checked between the dword-indexed
 * spawn (FUN_0046f914) and the byte-indexed tick (FUN_0046fbee) — see the
 * CS_NPC_OFF_* table in scene1_shop_walker.h.  Every rng_next15 / rng_next_unit
 * call below is in the engine's exact order.
 */

/* DAT_005c7ce0 — the (char_id, key) sprite-type registry, dumped from the
 * unpacked exe @ 0x5c7ce0 (18 pairs, then a 0xffffffff terminator).  Column 0
 * (char_id) is exposed by scene1_bg_npc_type_to_char; the roster scan matches
 * column 1 (key = the kyaku id in the session list).  The terminator is
 * &PTR_s_shop_jutan_umi_bmp_005c8000 in the decomp (end of the table). */
#define CS_SPRITE_TABLE_N 18
static const int32_t CS_SPRITE_TYPE_KEY[CS_SPRITE_TABLE_N] = {
    0x0c, 0x0e, 0x10, 0x02, 0x04, 0x03, 0x0b, 0x0d, 0x0f,
    0x11, 0x05, 0x07, 0x06, 0x08, 0x09, 0x12, 0x13, 0x14,
};

/* DAT_073a6e50.. — the NPC slot array (CS_NPC_MAX slots × CS_NPC_STRIDE dw). */
static int32_t s_cs_npc[CS_NPC_MAX * CS_NPC_STRIDE];

/* DAT_005c7dd0 — spawn cap (roster length).  DAT_073a7f30 — the roster of
 * matched table indices (one per eligible session customer). */
static int     s_cs_cap;                       /* DAT_005c7dd0 */
static int32_t s_cs_roster[CS_NPC_ROSTER_MAX]; /* DAT_073a7f30 */

/* DAT_073a8ba8 — per-frame counter; DAT_073a8bac — spawned-NPC count. */
static int     s_cs_frame;                      /* DAT_073a8ba8 */
static int     s_cs_spawned;                    /* DAT_073a8bac */

/* rng-phase drill instrumentation (the cs-walker-rng-phase residual): the last
 * pump's LCG-draw count, surfaced into the 0x48670f probe so flow_diff can pin
 * the exact frame the port's spawn/retarget draws diverge from retail's. */
static unsigned s_cs_last_draws;

/* DAT_0438b1a0 — config `s_easydisp` (default 0 → roster build enabled). */
static int     s_cs_easydisp;                   /* DAT_0438b1a0 */

/* helpers ───────────────────────────────────────────────────────────────── */

static float cs_slot_f(const int32_t *slot, int off)
{
    float f;
    memcpy(&f, &slot[off], sizeof f);
    return f;
}

static void cs_slot_set_f(int32_t *slot, int off, float v)
{
    memcpy(&slot[off], &v, sizeof v);
}

/* FUN_0046fbb7 — cell (x,y) walkable iff 0<=x<0x14, 0<=y<0xf, and the grid
 * cell DAT_074b28e8[x + y*0x14] is 0 (empty) or 9 (door/aisle).  The grid is
 * the SAME one the display chip rebuilds each frame. */
static int cs_cell_walkable(int x, int y)
{
    if (x < 0 || y < 0 || x >= 0x14 || y >= 0xf)
        return 0;
    int32_t cell = shop_display_grid_cell(x, y);
    return (cell == 0 || cell == 9) ? 1 : 0;
}

/* FUN_0046f892 @ 0x46f892 — NPC-array reset: each slot's [0x15] (active) = -1
 * and [0x17] (render scale) = 1.0f, then the frame/spawn counters = 0. */
void scene1_customer_npc_reset(void)
{
    memset(s_cs_npc, 0, sizeof s_cs_npc);
    for (int i = 0; i < CS_NPC_MAX; i++) {
        int32_t *slot = &s_cs_npc[i * CS_NPC_STRIDE];
        slot[CS_NPC_OFF_ACTIVE] = -1;                   /* puVar1[-2] = 0xffffffff */
        cs_slot_set_f(slot, CS_NPC_OFF_SCALE17, 1.0f);  /* *puVar1 = 0x3f800000 */
    }
    memset(s_cs_roster, 0, sizeof s_cs_roster);
    s_cs_cap     = 0;
    s_cs_frame   = 0;                                    /* DAT_073a8ba8 = 0 */
    s_cs_spawned = 0;                                    /* DAT_073a8bac = 0 */
}

int scene1_customer_npc_cap(void)     { return s_cs_cap; }
int scene1_customer_npc_spawned(void) { return s_cs_spawned; }
int scene1_customer_npc_frame(void)   { return s_cs_frame; }       /* DAT_073a8ba8 */
unsigned scene1_customer_npc_last_draws(void) { return s_cs_last_draws; }

int scene1_customer_npc_active(void)
{
    int n = 0;
    for (int i = 0; i < CS_NPC_MAX; i++)
        if (s_cs_npc[i * CS_NPC_STRIDE + CS_NPC_OFF_ACTIVE] != -1)
            n++;
    return n;
}

int32_t *scene1_customer_npc_slot(int idx)
{
    if (idx < 0 || idx >= CS_NPC_MAX)
        return NULL;
    return &s_cs_npc[idx * CS_NPC_STRIDE];
}

/* FUN_0046f8ba @ 0x46f8ba — roster/cap builder.  Walks the 0x14-entry session
 * list; for each entry, scans the (char_id,key) table for a key match, and on
 * a hit bumps the cap + appends the table index to the roster.  Stops at the
 * first negative list entry.  Gated on easydisp==0.  No RNG. */
void scene1_customer_npc_roster_build(const int32_t *session_list)
{
    s_cs_cap = 0;                                       /* DAT_005c7dd0 = 0 */
    if (s_cs_easydisp != 0 || session_list == NULL)
        return;

    for (int li = 0; li < 0x14; li++) {                 /* iVar3 != 0x14 */
        int32_t want = session_list[li];
        if (want < 0)                                   /* *param_1 < 0 → return */
            return;
        for (int ti = 0; ti < CS_SPRITE_TABLE_N; ti++) {  /* until 0xffffffff */
            if (CS_SPRITE_TYPE_KEY[ti] == want) {         /* ppuVar1[1] == *param_1 */
                if (s_cs_cap < CS_NPC_ROSTER_MAX)         /* DAT_073a7f30 span */
                    s_cs_roster[s_cs_cap] = ti;           /* *piVar4 = iVar2 */
                s_cs_cap++;                               /* DAT_005c7dd0++ */
                break;
            }
        }
    }
}

/* FUN_0046f914 @ 0x46f914 — spawn one NPC into the first free slot
 * ([0x15]==-1).  `type_idx` = the roster table index (param_1).  Draws RNG in
 * EXACTLY this order: rng15, rng15, rng_next_unit, rng15 (4 LCG draws total).
 *
 * Decompile quirk (replicated): the 2nd rng15's *return is discarded* and
 * [0x1f] is then set to `uVar4 & 1` where uVar4 is the LOOP INDEX (the free
 * slot's index), not the draw.  So [0x1f] ends as slot_index&1 but TWO LCG
 * draws are consumed before the float-rng.  `shop_tier` = DAT_04510578[stage]. */
static void cs_spawn_one(int type_idx, int shop_tier)
{
    for (int si = 0; si < CS_NPC_MAX; si++) {
        int32_t *slot = &s_cs_npc[si * CS_NPC_STRIDE];
        if (slot[CS_NPC_OFF_ACTIVE] != -1)              /* puVar3[0x15] == -1 */
            continue;

        slot[CS_NPC_OFF_ACTIVE] = type_idx;             /* [0x15] = param_1 */
        if (shop_tier == 0) {                           /* DAT_04510578[..]==0 */
            slot[CS_NPC_OFF_GRID_X] = 6;                /* [0x18] = 6 */
            slot[CS_NPC_OFF_GRID_Y] = 2;                /* [0x19] = 2 */
        } else {
            slot[CS_NPC_OFF_GRID_X] = 7;                /* [0x18] = 7 */
            slot[CS_NPC_OFF_GRID_Y] = 0;                /* [0x19] = 0 */
        }
        slot[CS_NPC_OFF_DETOUR]   = 1;                  /* [0x23] = 1 */
        cs_slot_set_f(slot, CS_NPC_OFF_POS_Y, 0.0f);    /* [0xc] = 0 */
        /* [0xb] = 2*grid_x - 9, [0xd] = 2*grid_y - 7 (engine fld of the int). */
        cs_slot_set_f(slot, CS_NPC_OFF_POS_X,
                      ((float)slot[CS_NPC_OFF_GRID_X]
                       + (float)slot[CS_NPC_OFF_GRID_X]) - 9.0f);
        cs_slot_set_f(slot, CS_NPC_OFF_POS_Z,
                      ((float)slot[CS_NPC_OFF_GRID_Y]
                       + (float)slot[CS_NPC_OFF_GRID_Y]) - 7.0f);
        slot[CS_NPC_OFF_FACING]   = 1;                  /* [6] = 1 */
        slot[CS_NPC_OFF_FLAG7]    = 0;                  /* [7] = 0 */
        slot[CS_NPC_OFF_FLAG8]    = 0;                  /* [8] = 0 */
        slot[CS_NPC_OFF_FLAG9]    = 0;                  /* [9] = 0 */
        slot[CS_NPC_OFF_TYPE_IDX] = type_idx;           /* [0x16] = param_1 */
        slot[CS_NPC_OFF_STATE]    = -1;                 /* [5] = 0xffffffff */

        /* ── RNG (exact engine order) ── */
        uint32_t r0 = rng_next15();                     /* draw #1 (thunk) */
        slot[CS_NPC_OFF_FLAGS] = (int32_t)(r0 & 1u);    /* [0x1f] = uVar2 & 1 */
        (void)rng_next15();                             /* draw #2 (return dropped) */
        slot[CS_NPC_OFF_FLAGS] = (int32_t)((uint32_t)si & 1u); /* [0x1f] = uVar4(=idx) & 1 */
        float fv = rng_next_unit();                     /* draw #3 (FUN_00471089) */
        cs_slot_set_f(slot, CS_NPC_OFF_SPEED, (fv + 1.0f) * 0.5f); /* [0x20] */
        uint32_t r3 = rng_next15();                     /* draw #4 (thunk) */
        slot[CS_NPC_OFF_PARAM21] = (int32_t)(r3 % 10u); /* [0x21] = uVar4 % 10 */
        slot[CS_NPC_OFF_ANIMCYCLE] = 0;                 /* [0x22] = 0 */

        /* FUN_00482a51(slot, 0): set anim 0 (resets frame/counter/timer when the
         * state changes off the -1 just written).  RNG-neutral. */
        if (slot[CS_NPC_OFF_STATE] != 0) {              /* param_1[5] != param_2(0) */
            slot[CS_NPC_OFF_FRAME]   = 0;               /* param_1[4] = 0 */
            slot[CS_NPC_OFF_COUNTER] = 0;               /* param_1[3] = 0 */
            slot[CS_NPC_OFF_STATE]   = 0;               /* param_1[5] = 0 */
            slot[CS_NPC_OFF_ANIM]    = 0;               /* *param_1   = 0 */
            slot[CS_NPC_OFF_TIMER]   = 0;               /* param_1[2] = 0 */
        }
        /* FUN_00482a71(slot, DAT_005c7ce0[type_idx*2], 1.0): one anim step.
         * RNG-neutral; chr_anim_tick is the port's FUN_00482a71. */
        chr_anim_tick(slot, scene1_bg_npc_type_to_char(type_idx), 1.0f);

        slot[CS_NPC_OFF_WSTATE] = -1;                   /* [0x1d] = 0xffffffff */
        slot[CS_NPC_OFF_WTIMER] = 0;                    /* [0x1e] = 0 */
        return;
    }
}

/* FUN_0046fa31 @ 0x46fa31 — next-step pathfinder toward (TGT_X,TGT_Y).  Draws
 * ONE rng15 (the `uVar4 & 7` 1/8 detour gate @ all.c:69211).  Returns a
 * direction code (the engine return is used only as the leaf's anim hint).
 * Ports the axis-walk + the %4 fallback loop verbatim. */
static int cs_pathfind_step(int32_t *slot)
{
    int gx = slot[CS_NPC_OFF_GRID_X];
    int gy = slot[CS_NPC_OFF_GRID_Y];
    int tx = slot[CS_NPC_OFF_TGT_X];
    int ty = slot[CS_NPC_OFF_TGT_Y];

    if (gx == tx) {                                     /* +0x60 == +0x68 */
        int step = (gy < ty) ? 1 : -1;                  /* +0x64 < +0x6c */
        int off = 0;
        for (int it = 0; it < 10; it++) {
            int probe = gy + off;
            if (probe == ty) {                          /* reached target row */
                slot[CS_NPC_OFF_GRID_Y] = gy + step;
                return step;
            }
            if (!cs_cell_walkable(gx, probe))
                break;
            off += step;
        }
    } else if (gy == ty) {                              /* +0x64 == +0x6c */
        int step = (gx < tx) ? 1 : -1;                  /* +0x60 < +0x68 */
        int off = 0;
        for (int it = 0; it < 10; it++) {
            int probe = gx + off;
            if (probe == tx) {
                slot[CS_NPC_OFF_GRID_X] = gx + step;
                return probe;
            }
            if (!cs_cell_walkable(probe, gy))
                break;
            off += step;
        }
    }

    /* detour / scan loop — 1/8 chance to bias the start direction (+3 or +5). */
    uint32_t r = rng_next15();                          /* the lone path RNG draw */
    int dir;
    if ((r & 7u) == 0) {
        dir = slot[CS_NPC_OFF_DETOUR]
            + (((uint32_t)slot[CS_NPC_OFF_FLAGS] & 1u) ? 5 : 3);
    } else {
        dir = slot[CS_NPC_OFF_DETOUR];
    }

    for (int it = 0; it < 4; it++) {
        int q = dir % 4;
        if (q == 0) {
            if (cs_cell_walkable(slot[CS_NPC_OFF_GRID_X] - 1, slot[CS_NPC_OFF_GRID_Y])) {
                slot[CS_NPC_OFF_GRID_X] -= 1;
                slot[CS_NPC_OFF_DETOUR] = 0;
                return 1;   /* engine returns the walkable result (nonzero) */
            }
        } else if (q == 1) {
            if (cs_cell_walkable(slot[CS_NPC_OFF_GRID_X], slot[CS_NPC_OFF_GRID_Y] + 1)) {
                slot[CS_NPC_OFF_GRID_Y] += 1;
                slot[CS_NPC_OFF_DETOUR] = 1;
                return 1;
            }
        } else if (q == 2) {
            if (cs_cell_walkable(slot[CS_NPC_OFF_GRID_X] + 1, slot[CS_NPC_OFF_GRID_Y])) {
                slot[CS_NPC_OFF_GRID_X] += 1;
                slot[CS_NPC_OFF_DETOUR] = 2;
                return 1;
            }
        } else { /* q == 3 */
            if (cs_cell_walkable(slot[CS_NPC_OFF_GRID_X], slot[CS_NPC_OFF_GRID_Y] - 1)) {
                slot[CS_NPC_OFF_GRID_Y] -= 1;
                slot[CS_NPC_OFF_DETOUR] = 3;
                return 1;
            }
        }
        /* engine: next dir = q + (flags&1 ? 5 : 3). */
        dir = q + (((uint32_t)slot[CS_NPC_OFF_FLAGS] & 1u) ? 5 : 3);
    }
    return dir / 4;
}

/* Camera-relative facing octant (engine FUN_0046fbee idle + FUN_0047019f
 * velocity recompute, asm 0x46fc52-0x46fce2 / 0x470300-0x47036b).  The raw
 * decompile drops the x87 octant conversion after the atan2/ftol; the real
 * formula is the SAME b850 converter the player uses:
 *   FACING = ftol(((angle + g_scene1_camera_yaw + π/8) / 2π)·8 + 8) & 7
 * (consts .rdata-verified: π/8=0x519b78, 2π=0x519398, 8=0x519378; cam yaw =
 * _DAT_073de39c).  RNG-neutral (render-only, no LCG draw). */
static int cs_npc_facing_octant(float angle)
{
    return player_ctrl_facing_octant(angle, g_scene1_camera_yaw);
}

/* FUN_0046fbee @ 0x46fbee — the per-NPC movement/wander tick.  `shop_tier` =
 * DAT_04510578[stage] (selects the state-machine z-clamp band).  Draws RNG only
 * in: state -1 (retarget burst, ≤30 iters × 2 LCG, break on first walkable
 * cell), state 0 (1 rng_next_unit heading + 1 rng15 inside the pathfinder).
 * States 1/2 + the position interp are RNG-neutral. */
static void cs_npc_tick(int32_t *slot, int shop_tier)
{
    int   wstate = slot[CS_NPC_OFF_WSTATE];             /* +0x74 */
    float old_x  = cs_slot_f(slot, CS_NPC_OFF_POS_X);   /* +0x2c */
    float old_z  = cs_slot_f(slot, CS_NPC_OFF_POS_Z);   /* +0x34 */

    if (wstate == -1) {
        /* ── retarget BURST: up to 30 tries, 2 LCG draws each ──
         * Break on the FIRST walkable cell (always); only when that cell has an
         * orthogonal furniture neighbour (grid ∈ [2,8]) does the NPC commit it
         * as the browse target + flip to state 0.  Otherwise WSTATE stays -1 and
         * next frame re-bursts — the engine's ±draw oscillation, which is why
         * the grid (DAT_074b28e8) MUST be modelled for RNG-exact counts. */
        slot[CS_NPC_OFF_TGT_X] = 1;                     /* +0x68 = 1 */
        slot[CS_NPC_OFF_TGT_Y] = 3;                     /* +0x6c = 3 */
        for (int it = 0; it < 0x1e; it++) {
            uint32_t ra = rng_next15();                 /* burst draw A */
            int cx = (int)(ra & 7u) + 1;                /* x ∈ [1,8] */
            uint32_t rb = rng_next15();                 /* burst draw B */
            int cy = (int)(rb % 7u) + 1;                /* y ∈ [1,7] */
            if (cs_cell_walkable(cx, cy)) {
                /* 4 orthogonal neighbour cells (engine DAT_074b28e4/ec/2898/2938
                 * = grid[idx-1]/[idx+1]/[idx-0x14]/[idx+0x14]); FACE_DIR is the
                 * last neighbour in the L,R,U,D order that is furniture. */
                int committed = 0;
                int cl = shop_display_grid_cell(cx - 1, cy);
                int cr = shop_display_grid_cell(cx + 1, cy);
                int cu = shop_display_grid_cell(cx, cy - 1);
                int cd = shop_display_grid_cell(cx, cy + 1);
                if (cl > 1 && cl < 9) { slot[CS_NPC_OFF_FACE_DIR] = 1; committed = 1; }
                if (cr > 1 && cr < 9) { slot[CS_NPC_OFF_FACE_DIR] = 3; committed = 1; }
                if (cu > 1 && cu < 9) { slot[CS_NPC_OFF_FACE_DIR] = 2; committed = 1; }
                if (cd > 1 && cd < 9) { slot[CS_NPC_OFF_FACE_DIR] = 0; committed = 1; }
                if (committed) {
                    slot[CS_NPC_OFF_WSTATE] = 0;        /* +0x74 = 0 */
                    slot[CS_NPC_OFF_TGT_X]  = cx;       /* +0x68 = cx */
                    slot[CS_NPC_OFF_TGT_Y]  = cy;       /* +0x6c = cy */
                }
                break;
            }
        }
    } else if (wstate == 0) {
        slot[CS_NPC_OFF_WTIMER] += 1;                   /* +0x78++ */
        /* FUN_00482a51(slot,..) on +0x58 != -1 — RNG-neutral set-anim, deferred
         * (render); +0x58 maps to no modeled anim slot here. */
        if (slot[CS_NPC_OFF_WTIMER] > 0) {
            slot[CS_NPC_OFF_WTIMER] = 0;                /* +0x78 = 0 */
            slot[CS_NPC_OFF_WSTATE] = 1;                /* +0x74 = 1 */
            float h = rng_next_unit();                  /* heading RNG draw */
            cs_slot_set_f(slot, CS_NPC_OFF_SPAWN_ANG, h * 6.2831855f); /* +0x50 */
            cs_pathfind_step(slot);                     /* +1 rng15 (FUN_0046fa31) */

            /* recompute world target from the (possibly stepped) grid cell. */
            float gx = (float)slot[CS_NPC_OFF_GRID_X];
            float gy = (float)slot[CS_NPC_OFF_GRID_Y];
            float wx = (gx + gx) - 9.0f;                /* +0x38 */
            float wz = (gy + gy) - 7.0f;                /* +0x40 */
            cs_slot_set_f(slot, CS_NPC_OFF_DRAW_X, wx);
            cs_slot_set_f(slot, CS_NPC_OFF_DRAW_Z, wz);

            float denom = (float)slot[CS_NPC_OFF_PARAM21] + 20.0f;  /* +0x84 + 20 */
            float vx = (wx - cs_slot_f(slot, CS_NPC_OFF_POS_X)) / denom;
            float vz = (wz - cs_slot_f(slot, CS_NPC_OFF_POS_Z)) / denom;
            if (vx < -0.1f) vx = -0.1f;                 /* clamp ±0.1 (0xbdcccccd) */
            if (vx >  0.1f) vx =  0.1f;
            if (vz < -0.1f) vz = -0.1f;
            if (vz >  0.1f) vz =  0.1f;
            cs_slot_set_f(slot, CS_NPC_OFF_VEL_X, vx);  /* +0x44 */
            cs_slot_set_f(slot, CS_NPC_OFF_VEL_Z, vz);  /* +0x4c */
        }
    } else if (wstate == 1) {
        slot[CS_NPC_OFF_WTIMER] += 1;                   /* +0x78++ */
        if (slot[CS_NPC_OFF_WTIMER] == slot[CS_NPC_OFF_PARAM21] + 0x14) {  /* +0x84+0x14 */
            slot[CS_NPC_OFF_WTIMER] = 0;
            slot[CS_NPC_OFF_WSTATE] = 0;                /* default back to 0 */
            slot[CS_NPC_OFF_ANIMCYCLE] += 1;            /* +0x88++ */
            if (slot[CS_NPC_OFF_ANIMCYCLE] == 5) {
                slot[CS_NPC_OFF_ANIMCYCLE] = 0;
                slot[CS_NPC_OFF_WSTATE]    = 2;         /* +0x74 = 2 */
            }
            if (slot[CS_NPC_OFF_GRID_X] == slot[CS_NPC_OFF_TGT_X] &&
                slot[CS_NPC_OFF_GRID_Y] == slot[CS_NPC_OFF_TGT_Y])
                slot[CS_NPC_OFF_WSTATE] = 2;            /* reached target → 2 */
        }
    } else if (wstate == 2) {
        /* FUN_00482a51(slot,0) on +0x58 != -1 (TYPE_IDX, always ≥0) — RNG-neutral
         * set-anim, deferred (render).  Then zero velocity + the dwell timer. */
        cs_slot_set_f(slot, CS_NPC_OFF_VEL_X, 0.0f);    /* +0x44 = 0 */
        cs_slot_set_f(slot, CS_NPC_OFF_VEL_Z, 0.0f);    /* +0x4c = 0 */
        /* facing octant (+0x18) from FACE_DIR (+0x70) — the engine's idle-dwell
         * facing (asm 0x46fc52-0x46fce2).  FACE_DIR → dir vector (fx=x, fy=z):
         * 0:(1,0) 1:(0,-1) 2:(-1,0) 3:(0,1); FACING = octant(atan2(fy,fx)).
         * RNG-neutral (render). */
        {
            float fx = 0.0f, fy = 0.0f;
            switch (slot[CS_NPC_OFF_FACE_DIR]) {        /* +0x70 */
                case 0: fx =  1.0f; break;
                case 1: fy = -1.0f; break;
                case 2: fx = -1.0f; break;
                case 3: fy =  1.0f; break;
                default: break;                         /* atan2(0,0) = 0 */
            }
            slot[CS_NPC_OFF_FACING] = cs_npc_facing_octant(atan2f(fy, fx));
        }
        slot[CS_NPC_OFF_WTIMER] += 1;                   /* +0x78++ */
        /* dwell: +0x58 (TYPE_IDX) is ALWAYS != -1 for a spawned NPC, so the
         * engine takes the 0x78 (120-frame) branch and retargets when
         * WTIMER > 0x78 (the signed `cmp WTIMER,0x78; !=&&>=` = strictly >). */
        if (slot[CS_NPC_OFF_WTIMER] > 0x78) {
            slot[CS_NPC_OFF_WTIMER] = 0;
            slot[CS_NPC_OFF_WSTATE] = -1;               /* +0x74 = -1 → retarget */
        }
    }

    /* ── position interp (RNG-neutral; engine all.c:69451-69505) ── */
    float speed = cs_slot_f(slot, CS_NPC_OFF_SPEED);    /* +0x80 */
    float vx = cs_slot_f(slot, CS_NPC_OFF_VEL_X);
    float vz = cs_slot_f(slot, CS_NPC_OFF_VEL_Z);
    float nx = speed * vx + cs_slot_f(slot, CS_NPC_OFF_POS_X);
    float nz = speed * vz + cs_slot_f(slot, CS_NPC_OFF_POS_Z);
    cs_slot_set_f(slot, CS_NPC_OFF_POS_X, nx);
    /* z hard-clamp band by shop tier (engine: tier<3 → 9.0, else 16.5). */
    if (shop_tier < 3) {
        if (nz > 9.0f) nz = 9.0f;                       /* 0x41100000 */
    } else {
        if (nz > 16.5f) nz = 16.5f;                     /* 0x41840000 */
    }
    cs_slot_set_f(slot, CS_NPC_OFF_POS_Z, nz);

    /* engine 69466-69497: an 8-step radial collision nudge against FUN_00433674
     * — geometry only, NO RNG.  The collision mesh for the chibi crowd is not
     * modeled (PORT-DEBT(cs-walker-collide)); it cannot perturb the LCG, so its
     * omission is RNG-safe.  Re-clamp the per-tick step magnitude to 0.2 like
     * the engine's trailing normalize (69498-69505). */
    float dx = cs_slot_f(slot, CS_NPC_OFF_POS_X) - old_x;
    float dz = cs_slot_f(slot, CS_NPC_OFF_POS_Z) - old_z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len > 0.2f) {
        cs_slot_set_f(slot, CS_NPC_OFF_POS_X, (dx * 0.2f) / len + old_x);
        cs_slot_set_f(slot, CS_NPC_OFF_POS_Z, (dz * 0.2f) / len + old_z);
    }
}

/* FUN_0047019f @ 0x47019f — the cc08==4 per-frame pump (RNG-consuming core).
 * Returns the number of LCG draws consumed this call (host RNG-accounting).
 *
 * PORT-DEBT(cs-walker-rng-phase): this closes the BULK of the cc08 first-customer
 * NPC RNG gap (the b534==2→0xf segment delta vs retail: −37 → ~−18), but NOT to
 * EXACT.  Residual is a burst draw-DISTRIBUTION mis-phase, NOT accepted phase:
 * verified port-vs-retail per sub-segment — plateau b534 2→6 is ~30 draws UNDER,
 * reaction b534 6→0xf ~12 OVER (they partially cancel, and the rng VALUE at the
 * BARGAIN still differs port≠retail).  Most likely the s_cs_frame (DAT_073a8ba8)
 * spawn-cadence phase at the first-customer cc08 entry (when/where it resets vs
 * the carried-over tutorial-cc08 counter), shifting which frames spawn (4 draws)
 * + retarget (burst) land.  Needs a port↔retail per-frame rngcalls drill on the
 * plateau to pin the exact spawn/retarget frames.  SEPARATE from the anchor-RNG-
 * pin-phase gap that blocks reproducing the recording's 2-sale flow (traversal). */
unsigned scene1_customer_npc_pump(int sell_inactive, int shop_tier)
{
    unsigned long rng0 = rng_call_count();

    s_cs_frame++;                                       /* DAT_073a8ba8++ */

    /* engine: FUN_005038ff + FUN_00451874 here build a "KYAKU:%d" debug HUD
     * string — RNG-neutral text formatting, render-only (PORT-DEBT(cs-hud)). */

    /* spawn cadence: a LIVE walk-in (f404==0) adds one NPC every 30th frame
     * while spawned<cap.  The f404==1 scripted tutorial is gated OUT. */
    if (sell_inactive) {                                /* (&DAT_0450f404)[..]=='\0' */
        if (s_cs_frame % 0x1e == 0) {                   /* every 30 frames */
            if (s_cs_spawned < s_cs_cap) {              /* DAT_073a8bac < DAT_005c7dd0 */
                int ri = (s_cs_spawned >= 0 && s_cs_spawned < CS_NPC_ROSTER_MAX)
                       ? s_cs_spawned : 0;
                cs_spawn_one(s_cs_roster[ri], shop_tier); /* FUN_0046f914 (4 LCG) */
            }
            s_cs_spawned++;                             /* DAT_073a8bac++ */
        }
    }

    /* tick every active NPC, then advance its sprite anim (RNG-neutral). */
    for (int si = 0; si < CS_NPC_MAX; si++) {
        int32_t *slot = &s_cs_npc[si * CS_NPC_STRIDE];
        if (slot[CS_NPC_OFF_ACTIVE] == -1)              /* piVar5[-1] == -1 → skip */
            continue;
        cs_npc_tick(slot, shop_tier);                   /* FUN_0046fbee */
        /* FUN_00482a71(slot, DAT_005c7ce0[type_idx*2], 1.0) — anim step. */
        chr_anim_tick(slot, scene1_bg_npc_type_to_char(slot[CS_NPC_OFF_TYPE_IDX]),
                      1.0f);
        /* engine: the type-0x42 special-NPC speech-bubble particle emit
         * (FUN_00447f4f, gated on DAT_005c7ce0[idx*2]==0x42 && DAT_0438b8cc%4==0)
         * follows — deferred (PORT-DEBT(cs-walker-special)); the 0x42 arm never
         * fires for the standard walk-in roster.  RNG-neutral.  The chibi SPRITE
         * render itself is ported (scene1_shop_walker.c
         * scene1_customer_npc_sprite_render / _shadow_render).
         *
         * velocity→facing-octant recompute (engine FUN_0047019f tail, asm
         * 0x470300-0x47036b): face the heading while moving; when stopped (both
         * vel == 0) skip — the wstate==2 FACE_DIR facing persists.  RNG-neutral. */
        {
            float vx = cs_slot_f(slot, CS_NPC_OFF_VEL_X);   /* +0x44 */
            float vz = cs_slot_f(slot, CS_NPC_OFF_VEL_Z);   /* +0x4c */
            if (vx != 0.0f || vz != 0.0f)                   /* 0x470302/0x470310 */
                slot[CS_NPC_OFF_FACING] =
                    cs_npc_facing_octant(atan2f(vx, vz));
        }
    }

    s_cs_last_draws = (unsigned)(rng_call_count() - rng0);
    return s_cs_last_draws;
}
