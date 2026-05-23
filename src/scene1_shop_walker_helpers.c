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

#include "math3d.h"
#include "scene1_records.h"

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
