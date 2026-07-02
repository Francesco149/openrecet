/*
 * scene1_per_frame_open.c — see scene1_per_frame_open.h.
 *
 * Engine sources:
 *   PFO.1: FUN_00414902 @ 0x414902 — Table A sentinel-init.
 *   PFO.2: FUN_00412a89 @ 0x412a89 L17-L42 — parent template table
 *          first init loop (per-entry default fill: -1 sentinels,
 *          100-quartet RGBA, 1.0 scale_mul, 0 xyz).
 *   PFO.3: FUN_00414929 @ 0x414929 L67-L195 — Table B per-tick body
 *          (anim-cell + per-type integrator + drag/gravity/age-kill).
 *   PFO.4: FUN_00414929 @ 0x414929 L128-L180 — SHAPE_MODE==4 +
 *          UNK_48!=0 shop-walker physics body (aim toward
 *          (11*factor, -9*factor, -520) with terminal kill + SE 0x29d).
 *
 * Other halves of FUN_00414929 land in PFO.5..PFO.7 per the chip
 * ladder in docs/findings/scene1-per-frame-open.md.
 */

#include "scene1_per_frame_open.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <stdio.h>
#include "storage.h"
#endif

#include "scene1_overlay.h"
#include "scene1_top_hud.h"   /* shake_pulse — the FUN_0040656e kill default */
#include "audio.h"            /* audio_play_se_by_id (FUN_00499519) — SE 0x29d */

int32_t g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_COUNT *
                             SCENE1_PFO_TABLE_A_STRIDE];

int32_t g_scene1_pfo_parent_table[SCENE1_PFO_PARENT_TABLE_COUNT *
                                  SCENE1_PFO_PARENT_TABLE_STRIDE];

void scene1_pfo_table_a_init(void)
{
    /* Engine FUN_00414902 L12548-L12552:
     *   puVar1 = &DAT_00730c30;
     *   do { *puVar1 = -1; puVar1 += 0xb; }
     *   while (puVar1 != &DAT_00733830);
     *
     * Each iteration writes the sentinel field (slot dw 4) to -1.
     * Other fields are NOT zeroed by the engine; our BSS-zero storage
     * leaves them at 0 which matches engine first-call behavior. */
    for (int i = 0; i < SCENE1_PFO_TABLE_A_COUNT; i++) {
        g_scene1_pfo_table_a[i * SCENE1_PFO_TABLE_A_STRIDE +
                             SCENE1_PFO_TABLE_A_OFF_SENTINEL] = -1;
    }
}

void scene1_pfo_parent_table_init(void)
{
    /* Engine FUN_00412a89 L18-L42 walks `puVar5 = &DAT_00744580` (=
     * entry+40 dw) stepping +0x5f per iter, and per entry runs a
     * 7-iter inner loop with strided pointer arithmetic:
     *
     *   puVar5+-15+k : sub_rec[k].sentinel  = -1
     *   puVar5+-8+k  : sub_rec[k].age_match = 0
     *   puVar5+(4k-1, 4k, 4k+1, 4k+2) : sub_rec[k].rgba = (100, 100, 100, 100)
     *   puVar5+0x1b+k : sub_rec[k].scale_mul = 1.0f
     *   puVar5+0x22+3k .. +0x24+3k : sub_rec[k].xyz = (0, 0, 0)
     *
     * Re-anchored to entry start (entry+0..94): sentinels at dw
     * 25..31; age_match at 32..38; rgba at 39..66 (4 dw × 7);
     * scale_mul at 67..73; xyz at 74..94 (3 dw × 7).
     *
     * The engine also writes "<unknown>" to the name field at entry+0
     * via FUN_005038ff (sprintf-style).  The tick (FUN_00414929)
     * never reads the name; PFO.7's parser overwrites entry+0..24
     * from `ef/effect%d.dat`.  We leave dw 0..24 BSS-zero. */

    const int32_t one_f = 0x3f800000; /* IEEE 754 binary32 1.0f */
    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        int32_t *entry = &g_scene1_pfo_parent_table[
            i * SCENE1_PFO_PARENT_TABLE_STRIDE];
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            entry[SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0  + k]      = -1;
            entry[SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0 + k]      = 0;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 0] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 1] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 2] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0      + k * 4 + 3] = 100;
            entry[SCENE1_PFO_PARENT_OFF_SUB_SCALE_MUL_0 + k]      = one_f;
            entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 0] = 0;
            entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 1] = 0;
            entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 2] = 0;
        }
    }
}

/* ===== PFO.3 — Table B per-tick body ====================================
 *
 * Direct port of FUN_00414929 L67-L195.  See scene1_per_frame_open.h for
 * the chip-level writeup.  Engine-side annotations point to specific
 * decomp lines / asm offsets.
 *
 * Field-perspective note: this function uses tick-perspective names for
 * the slot fields where the renderer/tick disagree:
 *   - OFF_BEND_X/Y/Z (renderer "bend") = tick "vel"
 *   - OFF_VEL_X/Y/Z  (renderer "vel")  = tick "accum" for SHAPE_MODE 1/6
 *   - OFF_POS_X_COPY/Y/Z = type-1/6 base anchor.
 */

static inline float bits_to_f(int32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static inline int32_t f_to_bits(float f)
{
    int32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return bits;
}

static inline int32_t slot_b_i(int s, int off)
{
    return g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off];
}
static inline void slot_b_set_i(int s, int off, int32_t v)
{
    g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off] = v;
}
static inline float slot_b_f(int s, int off)
{
    return bits_to_f(slot_b_i(s, off));
}
static inline void slot_b_set_f(int s, int off, float v)
{
    slot_b_set_i(s, off, f_to_bits(v));
}

/* Read 3 floats from an int-stored owner pointer at a fixed byte
 * offset.  Engine convention: OWNER_A/B is a host pointer cast to int32
 * (target ABI is 32-bit Windows).  On 64-bit host tests, callers that
 * exercise this path must ensure the buffer's address fits in int32_t
 * (use static .bss arrays + a non-PIE build, or stub OWNER as 0).  When
 * the slot's owner field is 0 the matrix add collapses to zero — that
 * matches the engine behavior for sentinel-empty owner pointers.
 *
 * NOTE: the engine's vendor binary genuinely derefs a null pointer when
 * OWNER is 0 and SHAPE_MODE is 1/6, which would crash — but this only
 * happens when a SHAPE_MODE-1/6 spawn drops a slot with NULL owner.  No
 * such spawn site exists in HOUSE today; the guard here is a safety
 * net, not a behavior divergence in any reachable code path. */
static void owner_matrix_xyz_read(int32_t owner_int, size_t byte_off,
                                  float *out_x, float *out_y, float *out_z)
{
    if (owner_int == 0) {
        *out_x = 0.0f;
        *out_y = 0.0f;
        *out_z = 0.0f;
        return;
    }
    const char *base = (const char *)(intptr_t)owner_int;
    memcpy(out_x, base + byte_off + 0, sizeof *out_x);
    memcpy(out_y, base + byte_off + 4, sizeof *out_y);
    memcpy(out_z, base + byte_off + 8, sizeof *out_z);
}

void scene1_pfo_table_b_tick(void)
{
    for (int s = 0; s < SCENE1_OVERLAY_SLOT_COUNT; s++) {
        /* Engine L70: `if (piVar2[-4] != -1)` — slot ACTIVE != -1. */
        if (slot_b_i(s, SCENE1_OVERLAY_OFF_ACTIVE) == -1) continue;

        /* ── Step 1: anim-cell tick (engine L71-L87, always runs). ──── */
        int32_t shape_idx = slot_b_i(s, SCENE1_OVERLAY_OFF_TEXTURE_TYPE);
        int32_t fc_new = slot_b_i(s, SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER) + 1;
        slot_b_set_i(s, SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER, fc_new);

        /* Engine reads (&DAT_00769768)[iVar10*8] etc. without bounds check;
         * we add a typed-storage bounds guard.  HOUSE has shape_idx in
         * range so this never differs in practice. */
        if (shape_idx >= 0 && shape_idx < SCENE1_OVERLAY_SHAPE_COUNT) {
            const int32_t *shape = &g_scene1_overlay_shapes[
                shape_idx * SCENE1_OVERLAY_SHAPE_STRIDE];
            int32_t period = shape[SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD];
            /* Engine L74: `0 < frame_period && frame_period <= fc_new`. */
            if (period > 0 && period <= fc_new) {
                int32_t cell_count = shape[SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT];
                /* Reset frame counter, advance cell index. */
                slot_b_set_i(s, SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER, 0);
                int32_t cell_new = slot_b_i(s, SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX) + 1;
                slot_b_set_i(s, SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX, cell_new);
                /* Engine L79: `if (iVar9 <= *piVar2)` — cell_count <= cell_new. */
                if (cell_count <= cell_new) {
                    /* L80: LOOP_MODE==1 → wrap to 0; else clamp to last. */
                    if (shape[SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE] == 1) {
                        slot_b_set_i(s, SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX, 0);
                    } else {
                        slot_b_set_i(s, SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX,
                                     cell_count - 1);
                    }
                }
            }
        }

        /* ── Step 2: type-dispatched integrator (engine L88-L182).
         * Gated on AGE >= 0 — spawn API plants negative ages for
         * staggered bursts.  AGE pre-increment is read here. ──── */
        int32_t age        = slot_b_i(s, SCENE1_OVERLAY_OFF_AGE);
        int32_t shape_mode = slot_b_i(s, SCENE1_OVERLAY_OFF_SHAPE_MODE);
        int32_t type_shape = slot_b_i(s, SCENE1_OVERLAY_OFF_TYPE_SHAPE);

        if (age >= 0) {
            /* "vel" = BEND_X/Y/Z (renderer's "bend" slot fields).
             * Snapshot at top so type-dispatch and drag-mutate read
             * consistent pre-drag values. */
            float bx = slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_X);
            float by = slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Y);
            float bz = slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Z);

            if (shape_mode == 1 || shape_mode == 6) {
                /* Type 1/6: accum (= VEL_X/Y/Z) += vel; pos = base
                 * (POS_X_COPY/Y/Z) + matrix-row + accum.
                 * - shape_mode==1: matrix is OWNER_A + 0x20..+0x28.
                 * - shape_mode==6: matrix is OWNER_B + 0x3f0..+0x3f8.
                 *
                 * Engine L90-101 (type 1) / L105-114 (type 6). */
                float ax = slot_b_f(s, SCENE1_OVERLAY_OFF_VEL_X) + bx;
                float ay = slot_b_f(s, SCENE1_OVERLAY_OFF_VEL_Y) + by;
                float az = slot_b_f(s, SCENE1_OVERLAY_OFF_VEL_Z) + bz;
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_VEL_X, ax);
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_VEL_Y, ay);
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_VEL_Z, az);

                int   owner_off = (shape_mode == 1)
                    ? SCENE1_OVERLAY_OFF_OWNER_A
                    : SCENE1_OVERLAY_OFF_OWNER_B;
                size_t mat_off  = (shape_mode == 1) ? 0x20 : 0x3f0;
                float mx, my, mz;
                owner_matrix_xyz_read(slot_b_i(s, owner_off), mat_off,
                                      &mx, &my, &mz);

                slot_b_set_f(s, SCENE1_OVERLAY_OFF_POS_X,
                             slot_b_f(s, SCENE1_OVERLAY_OFF_POS_X_COPY) + mx + ax);
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_POS_Y,
                             slot_b_f(s, SCENE1_OVERLAY_OFF_POS_Y_COPY) + my + ay);
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_POS_Z,
                             slot_b_f(s, SCENE1_OVERLAY_OFF_POS_Z_COPY) + mz + az);
            } else if (type_shape == 8 || type_shape == 9 ||
                       type_shape == 10) {
                /* Engine L122: type_shape in {8,9,10} → ROT_Y += BEND_Y. */
                float ry = slot_b_f(s, SCENE1_OVERLAY_OFF_ROT_Y) + by;
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_ROT_Y, ry);
            } else {
                /* Engine L117-L120: default → pos += BEND. */
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_POS_X,
                             slot_b_f(s, SCENE1_OVERLAY_OFF_POS_X) + bx);
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_POS_Y,
                             slot_b_f(s, SCENE1_OVERLAY_OFF_POS_Y) + by);
                slot_b_set_f(s, SCENE1_OVERLAY_OFF_POS_Z,
                             slot_b_f(s, SCENE1_OVERLAY_OFF_POS_Z) + bz);
            }

            /* Drag (engine L124-126): BEND *= TEMPLATE5_COPY. */
            float drag = slot_b_f(s, SCENE1_OVERLAY_OFF_TEMPLATE5_COPY);
            slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_X, bx * drag);
            slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y, by * drag);
            slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Z, bz * drag);

            /* Gravity-like additive (engine L127): BEND_Y += UNK_48. */
            float gravity = slot_b_f(s, SCENE1_OVERLAY_OFF_UNK_48);
            slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
                         slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Y) + gravity);

            /* PFO.4: SHAPE_MODE==4 + UNK_48 != 0 "shop walker" aim
             * physics (engine L128-L180; asm 0x414bf3..0x414e7e).
             * Walks toward the fixed off-screen point
             * (11*factor, -9*factor, -520) where factor is the engine's
             * clamp-at-1.2 quirk #50 (always 1.2 once the gate opens).
             * Terminal kill fires on |target-pos|<0.5 or pos.y<target.y;
             * the engine then calls FUN_0040656e (SE 0x29d + screen
             * shake), which is host-installable here via the kill hook.
             *
             * Dormant in HOUSE — no spawn site populates type-4 with
             * non-zero UNK_48 today. */
            if (shape_mode == 4 && slot_b_f(s, SCENE1_OVERLAY_OFF_UNK_48) != 0.0f) {
                /* Engine L129 / asm 0x414c0f-0x414c20: gate on
                 * `30 + (slot_idx % 4) < AGE`.  slot_idx is the engine's
                 * `local_2c` outer iter counter — initialized to 0 at
                 * function entry, incremented per slot.  Maps 1:1 to
                 * our loop variable `s`. */
                int gate = 30 + (s % 4);
                if (age > gate) {
                    /* Engine quirk #50: factor = (AGE-30)*0.4 + 1.2,
                     * clamped at 1.2 max.  AGE>30 → factor≥1.6 → ALWAYS
                     * clamped to 1.2 in this branch.  We preserve the
                     * formula verbatim so the post-clamp `factor==1.2`
                     * test is bit-exact (clamp loads the .rdata 1.2
                     * constant, so the compare is exact bit equality). */
                    float factor = (float)(age - 30) * 0.4f + 1.2f;
                    if (factor > 1.2f) factor = 1.2f;
                    float target_y = factor * -9.0f;       /* = -10.8 */
                    float target_x = factor *  11.0f;      /* =  13.2 */
                    const float target_z = -520.0f;

                    /* UNK_48 *= 0.8 (engine L135 / asm 0x414c75-0x414c7e).
                     * Subsequent reads of UNK_48 in this iteration must
                     * see the decayed value (matches engine — the *=0.8
                     * stores BEFORE the pos.y<target.y vel.y -= UNK_48
                     * read). */
                    float unk_48_old = slot_b_f(s, SCENE1_OVERLAY_OFF_UNK_48);
                    float unk_48     = unk_48_old * 0.8f;
                    slot_b_set_f(s, SCENE1_OVERLAY_OFF_UNK_48, unk_48);

                    /* Raw deltas (engine L138-L142 / asm 0x414c81-0x414c9c).
                     * Saved for the post-step terminal distance check. */
                    float pos_x = slot_b_f(s, SCENE1_OVERLAY_OFF_POS_X);
                    float pos_y = slot_b_f(s, SCENE1_OVERLAY_OFF_POS_Y);
                    float pos_z = slot_b_f(s, SCENE1_OVERLAY_OFF_POS_Z);
                    float dx_raw = target_x - pos_x;
                    float dy_raw = target_y - pos_y;
                    float dz_raw = target_z - pos_z;

                    /* Scaled deltas (engine L136-L138 / asm 0x414c9f-0x414cc0).
                     * Note z uses *0.2 (faster pull on z), x/y use *0.1. */
                    float dx = dx_raw * 0.1f;
                    float dy = dy_raw * 0.1f;
                    float dz = dz_raw * 0.2f;

                    /* Normalize scaled delta to 0.1 if magnitude > 0.1
                     * (engine L139-L145 / asm 0x414cc3-0x414d24). */
                    float dmag = sqrtf(dx*dx + dy*dy + dz*dz);
                    if (dmag > 0.1f) {
                        dx = (dx * 0.1f) / dmag;
                        dy = (dy * 0.1f) / dmag;
                        dz = (dz * 0.1f) / dmag;
                    }

                    /* vel += delta (engine L146-L148 / asm 0x414d27-0x414d3f). */
                    slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_X,
                                 slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_X) + dx);
                    slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
                                 slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Y) + dy);
                    slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Z,
                                 slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Z) + dz);

                    /* AGE > 40 → gradual drag scale (engine L149-L157 /
                     * asm 0x414d42-0x414d97).  Walks from 1.0 down at
                     * 0.002/tick to a floor of 0.97. */
                    if (age > 40) {
                        float drag2 = 1.0f - (float)(age - 40) * 0.002f;
                        if (drag2 < 0.97f) drag2 = 0.97f;
                        slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_X,
                                     slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_X) * drag2);
                        slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
                                     slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Y) * drag2);
                        slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Z,
                                     slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Z) * drag2);
                    }

                    /* |vel|² > 0 → if |vel| > 1.0 normalize to unit
                     * (engine L158-L168 / asm 0x414d9a-0x414e11).  The
                     * |vel|²>0 gate just dodges divide-by-zero. */
                    float vx = slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_X);
                    float vy = slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Y);
                    float vz = slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Z);
                    float vsq = vx*vx + vy*vy + vz*vz;
                    if (vsq > 0.0f) {
                        float vmag = sqrtf(vsq);
                        if (vmag > 1.0f) {
                            slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_X, vx / vmag);
                            slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y, vy / vmag);
                            slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Z, vz / vmag);
                        }
                    }

                    /* pos.y < target.y → BEND_Y -= UNK_48 (engine L169-L171
                     * / asm 0x414e14-0x414e25).  Cancels the gravity step
                     * that already added UNK_48 to BEND_Y, but with the
                     * decayed UNK_48 — net ~0.2*UNK_48 still acts as
                     * downward force while below the target. */
                    if (pos_y < target_y) {
                        slot_b_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
                                     slot_b_f(s, SCENE1_OVERLAY_OFF_BEND_Y) - unk_48);
                    }

                    /* Terminal check (engine L172-L178 / asm 0x414e28-0x414e7e).
                     * factor==1.2 always holds in this branch (quirk #50)
                     * — the gate is structurally dead in the asm but we
                     * preserve it for bit-exact behavior. */
                    if (factor == 1.2f) {
                        float dist_target = sqrtf(dx_raw*dx_raw +
                                                  dy_raw*dy_raw +
                                                  dz_raw*dz_raw);
                        if (dist_target < 0.5f || pos_y < target_y) {
                            slot_b_set_i(s, SCENE1_OVERLAY_OFF_ACTIVE, -1);
                            scene1_pfo_fire_type_4_terminal_kill(s);
                        }
                    }
                }
            }

            /* Energy decay (engine L181): SCALE_X += TEMPLATE11_COPY. */
            float energy = slot_b_f(s, SCENE1_OVERLAY_OFF_SCALE_X);
            float delta  = slot_b_f(s, SCENE1_OVERLAY_OFF_TEMPLATE11_COPY);
            slot_b_set_f(s, SCENE1_OVERLAY_OFF_SCALE_X, energy + delta);
        }

        /* ── Step 3: AGE++ and kill check (engine L183-L188).
         * BOTH run regardless of the AGE >= 0 gate above. ──── */
        int32_t age_new = age + 1;
        slot_b_set_i(s, SCENE1_OVERLAY_OFF_AGE, age_new);

        float unk_48 = slot_b_f(s, SCENE1_OVERLAY_OFF_UNK_48);
        int   type_4_active = (shape_mode == 4 && unk_48 != 0.0f);
        int32_t fade_off  = slot_b_i(s, SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET);
        int32_t age_birth = slot_b_i(s, SCENE1_OVERLAY_OFF_AGE_BIRTH);
        float   energy    = slot_b_f(s, SCENE1_OVERLAY_OFF_SCALE_X);
        if (!type_4_active && fade_off != -1) {
            if (fade_off <= age_new - age_birth || energy <= 0.0f) {
                slot_b_set_i(s, SCENE1_OVERLAY_OFF_ACTIVE, -1);
            }
        }
    }
}

/* ===== PFO.4 — terminal kill (engine FUN_0040656e) ==================== */

static void (*g_pfo_type_4_kill_hook)(int) = NULL;

void scene1_pfo_set_type_4_terminal_kill_hook(void (*hook)(int slot_idx))
{
    g_pfo_type_4_kill_hook = hook;
}

void scene1_pfo_clear_type_4_terminal_kill_hook(void)
{
    g_pfo_type_4_kill_hook = NULL;
}

void scene1_pfo_fire_type_4_terminal_kill(int slot_idx)
{
    if (g_pfo_type_4_kill_hook) {         /* test override */
        g_pfo_type_4_kill_hook(slot_idx);
        return;
    }
    /* Production default = the real FUN_0040656e: DAT_00648280 = 4 +
     * SE 0x29d — one pulse per landing sale coin (RE §21.31.2). */
    scene1_top_hud_shake_pulse();
    audio_play_se_by_id(0x29d);           /* FUN_00499519(0x29d) */
}

/* ===== PFO.5a — Table A per-tick body ================================ */

int32_t g_scene1_pfo_alt_mode = 0;  /* PHC #17 stand-in (DAT_074b2ee4). */

static scene1_pfo_spawn_hook_fn g_pfo_spawn_hook = NULL;

void scene1_pfo_set_spawn_hook(scene1_pfo_spawn_hook_fn hook)
{
    g_pfo_spawn_hook = hook;
}

void scene1_pfo_clear_spawn_hook(void)
{
    g_pfo_spawn_hook = NULL;
}

static void pfo_invoke_spawn(const void *template_owner,
                             float pos_x, float pos_y, float pos_z,
                             int   template_id,
                             float scale_base,
                             int   override_dur,
                             int   override_rot_y,
                             int   shape_mode,
                             int   mode)
{
    if (g_pfo_spawn_hook) {
        g_pfo_spawn_hook(template_owner, pos_x, pos_y, pos_z,
                         template_id, scale_base,
                         override_dur, override_rot_y,
                         shape_mode, mode);
    } else {
        scene1_overlay_spawn(template_owner, pos_x, pos_y, pos_z,
                             template_id, scale_base,
                             override_dur, override_rot_y,
                             shape_mode, mode);
    }
}

static inline int32_t slot_a_i(int s, int off)
{
    return g_scene1_pfo_table_a[s * SCENE1_PFO_TABLE_A_STRIDE + off];
}
static inline void slot_a_set_i(int s, int off, int32_t v)
{
    g_scene1_pfo_table_a[s * SCENE1_PFO_TABLE_A_STRIDE + off] = v;
}
static inline float slot_a_f(int s, int off)
{
    return bits_to_f(slot_a_i(s, off));
}

void scene1_pfo_table_a_tick(void)
{
    /* Direct port of engine FUN_00414929 L23-L66 / asm 0x414932..0x414aa1.
     *
     * Engine walks at sentinel anchor `piVar2 = &DAT_00730c30` (= slot
     * dw 4 of slot 0).  Our typed storage is contiguous (slot 0..255);
     * we iterate slot index `s` and read `slot[OFF_SENTINEL]`. */

    for (int s = 0; s < SCENE1_PFO_TABLE_A_COUNT; s++) {
        int32_t parent_id = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_SENTINEL);
        if (parent_id == -1) continue;  /* L24-L25: skip empty. */

        /* Bounds-guard the parent_id read — engine indexes blindly into
         * the 400-entry parent table.  No real allocator should produce
         * an out-of-range id; treat OOB as "no live sub-records" so the
         * age tick still runs (matching engine when sub-record sentinel
         * happens to be -1). */
        if (parent_id < 0 ||
            parent_id >= SCENE1_PFO_PARENT_TABLE_COUNT) {
            goto age_tick;
        }

        /* L26-L29: inner sub-record walk anchored at parent_table[parent_id]
         * dw 25 (= sub_rec[0].sentinel) and dw 75 (= sub_rec[0].xyz_y).
         * Per iteration k=0..6: read sentinel, age_match, scale_mul, xyz. */
        int32_t age = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_AGE);

        const int32_t *entry = &g_scene1_pfo_parent_table[
            parent_id * SCENE1_PFO_PARENT_TABLE_STRIDE];

        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            int32_t sub_sentinel  = entry[SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0  + k];
            int32_t sub_age_match = entry[SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0 + k];
            /* L30-L31: gate sub_rec[k].sentinel != -1 AND age_match == age. */
            if (sub_sentinel == -1) continue;
            if (sub_age_match != age) continue;

            int32_t sub_scale_mul_bits = entry[SCENE1_PFO_PARENT_OFF_SUB_SCALE_MUL_0 + k];
            float   sub_scale_mul = bits_to_f(sub_scale_mul_bits);
            float   sub_x = bits_to_f(entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0 + k * 3 + 0]);
            float   sub_y = bits_to_f(entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0 + k * 3 + 1]);
            float   sub_z = bits_to_f(entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0 + k * 3 + 2]);

            /* L32-L35: alt_offset (= local_2c) = 0, but -520 when
             * g_scene1_pfo_alt_mode (= DAT_074b2ee4) != 0.  Only used by
             * the passthrough (mode==0) arm. */
            float alt_offset = (g_scene1_pfo_alt_mode != 0) ? -520.0f : 0.0f;

            int32_t mode_flag = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_MODE);
            float   slot_p1   = slot_a_f(s, SCENE1_PFO_TABLE_A_OFF_PARAM1);
            float   slot_p2   = slot_a_f(s, SCENE1_PFO_TABLE_A_OFF_PARAM2);
            float   slot_p3   = slot_a_f(s, SCENE1_PFO_TABLE_A_OFF_PARAM3);
            float   slot_p5   = slot_a_f(s, SCENE1_PFO_TABLE_A_OFF_PARAM5);
            int32_t slot_p6   = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM6);
            int32_t slot_p7   = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM7);
            int32_t slot_p0   = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM0);
            /* shape_mode arg = PARAM8, NOT MODE: both spawn arms push
             * `[esi+0x10]` = slot dw8 (esi anchors at the SENTINEL dw4;
             * asm 0x41499c projected / 0x414a1f passthrough).  The old
             * slot[10] transcription starved the sale coin shower of its
             * SHAPE_MODE=4 aim/landing physics (RE §21.31.2). */
            int32_t shape_mode_arg = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM8);

            float scale_base = slot_p5 * sub_scale_mul;
            float pos_x, pos_y, pos_z;
            const void *template_owner;
            int   override_rot_y_bits;
            int   mode_arg;

            if (mode_flag == 0) {
                /* L36-L43: passthrough.
                 * pos_x = sub.x + slot[1]
                 * pos_y = slot[2] + sub.y
                 * pos_z = sub.z + slot[3] + alt_offset
                 * template_owner = slot[0]
                 * override_rot_y = slot[7] (as float bits)
                 * shape_mode arg = slot[8] (PARAM8)
                 * mode arg       = 0
                 *
                 * Asm 0x414a0f..0x414a6c verified push order. */
                pos_x = sub_x + slot_p1;
                pos_y = slot_p2 + sub_y;
                pos_z = sub_z + slot_p3 + alt_offset;
                template_owner = (const void *)(intptr_t)slot_p0;
                override_rot_y_bits = slot_p7;
                mode_arg = 0;
            } else {
                /* L45-L52: projected.
                 * pos_x = 16.5 - (slot[1] + sub.x) / 19.5
                 * pos_y = 12.4 - (slot[2] + sub.y) / 19.5
                 * pos_z = -520.0
                 * template_owner = 0
                 * override_rot_y = 0.0f
                 * shape_mode arg = slot[8] (PARAM8)
                 * mode arg       = 1
                 *
                 * Asm 0x414991..0x414a0d verified. */
                pos_x = 16.5f - (slot_p1 + sub_x) / 19.5f;
                pos_y = 12.4f - (slot_p2 + sub_y) / 19.5f;
                pos_z = -520.0f;
                template_owner = NULL;
                override_rot_y_bits = f_to_bits(0.0f);
                mode_arg = 1;
            }

            pfo_invoke_spawn(template_owner,
                             pos_x, pos_y, pos_z,
                             /*template_id=*/sub_sentinel,
                             scale_base,
                             /*override_dur=*/slot_p6,
                             /*override_rot_y=*/override_rot_y_bits,
                             /*shape_mode=*/shape_mode_arg,
                             /*mode=*/mode_arg);
        }

    age_tick:
        /* L60-L63: age++ ; if (age == 300) sentinel = -1 (self-clear).
         * Engine pre-increments and tests post-inc; mirrored here. */
        {
            int32_t age_new = slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_AGE) + 1;
            slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_AGE, age_new);
            if (age_new == 300) {
                slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_SENTINEL, -1);
            }
        }
    }
}

/* ===== PFO.6 — Table A allocators ====================================
 *
 * Both allocators are short linear scans of Table A for the first
 * SENTINEL == -1 slot, followed by a fixed-pattern field fill.  Engine
 * sources at by-address/4132c1.c + by-address/41331d.c.  Asm verified
 * at 0x4132c1..0x413315 / 0x41331d..0x413375. */

int scene1_pfo_table_a_alloc_projected(float pos_x, float pos_y,
                                       int   template_id,
                                       float scale_base,
                                       int   override_dur,
                                       int   param_8)
{
    /* Engine puVar1 = &DAT_00730c20; do while puVar1 != &DAT_00733820. */
    for (int s = 0; s < SCENE1_PFO_TABLE_A_COUNT; s++) {
        if (slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_SENTINEL) != -1) continue;

        /* L11-L24: per-field write pattern.
         * slot[3] = -520.0f via .rdata constant 0xc4020000.
         * slot[7] = 0 (engine fldz; fstp → IEEE-754 0.0 = bit-pattern 0).
         * MODE = 1 (projected). */
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM0,   0);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM1,   f_to_bits(pos_x));
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM2,   f_to_bits(pos_y));
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM3,   (int32_t)0xc4020000);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_SENTINEL, template_id);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM5,   f_to_bits(scale_base));
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM6,   override_dur);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM7,   0);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM8,   param_8);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_AGE,      0);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_MODE,     1);
        return s;
    }
    /* Table full — engine returns without writing.  Mirror via -1. */
    return -1;
}

int scene1_pfo_table_a_alloc_passthrough(int   template_owner,
                                         float pos_x, float pos_y, float pos_z,
                                         int   template_id,
                                         float scale_base,
                                         int   override_dur,
                                         int   override_rot_y_bits,
                                         int   param_8)
{
    for (int s = 0; s < SCENE1_PFO_TABLE_A_COUNT; s++) {
        if (slot_a_i(s, SCENE1_PFO_TABLE_A_OFF_SENTINEL) != -1) continue;

        /* All 9 caller-arg fields fill in directly; AGE = 0; MODE = 0
         * (passthrough). */
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM0,   template_owner);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM1,   f_to_bits(pos_x));
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM2,   f_to_bits(pos_y));
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM3,   f_to_bits(pos_z));
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_SENTINEL, template_id);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM5,   f_to_bits(scale_base));
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM6,   override_dur);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM7,   override_rot_y_bits);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_PARAM8,   param_8);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_AGE,      0);
        slot_a_set_i(s, SCENE1_PFO_TABLE_A_OFF_MODE,     0);
        return s;
    }
    return -1;
}

/* ===== PFO.7 — parent template binary blob loader ====================
 *
 * Port of FUN_00412a89 L70-L86 file-loading loop.  Engine survey claim
 * (FUN_0041276e parser) corrected to dead-code finding: see header.
 *
 * On-disk layout per file:
 *
 *   bytes      0 .. 17199   secondary table chunk (172 B × 100 entries).
 *                            Not ported here — no in-port consumer reads
 *                            from the engine's `DAT_00733820` table yet.
 *   bytes  17200 .. 55199   parent template chunk (380 B × 100 entries).
 *                            Copied verbatim into g_scene1_pfo_parent_table
 *                            at slot file_idx*100.
 *
 * The parent template chunk is binary identical to the in-memory
 * struct layout (verified: entry 0 of effect1.dat starts with "unknown"
 * at offset 0, matches engine init default).
 */

#define PFO_SECONDARY_CHUNK_BYTES  17200u   /* engine fread #1 size: 0x4330 */
#define PFO_PARENT_CHUNK_BYTES     38000u   /* engine fread #2 size */
#define PFO_PARENT_FILE_COUNT      4

void scene1_pfo_parent_table_load_chunk(int file_idx,
                                        const void *chunk,
                                        size_t chunk_len)
{
    if (file_idx < 0 || file_idx >= PFO_PARENT_FILE_COUNT) return;
    if (chunk == NULL) return;

    /* Clamp to a single file's slice; engine fread caps at 38000. */
    size_t bytes = chunk_len;
    if (bytes > PFO_PARENT_CHUNK_BYTES) bytes = PFO_PARENT_CHUNK_BYTES;
    if (bytes == 0) return;

    /* One file slice = 100 entries × 95 dw × 4 = 38000 B; this is also
     * exactly 1/4 of the parent_table backing storage.  Compile-time
     * cross-check: */
    _Static_assert(PFO_PARENT_CHUNK_BYTES ==
                       (size_t)SCENE1_PFO_PARENT_TABLE_STRIDE * 4u * 100u,
                   "PFO chunk size must match 100 entries × 95 dw × 4 B");
    _Static_assert(PFO_PARENT_FILE_COUNT * 100 ==
                       SCENE1_PFO_PARENT_TABLE_COUNT,
                   "PFO parent table sized for 4 × 100 entries");

    uint8_t *dst = (uint8_t *)&g_scene1_pfo_parent_table[
        file_idx * 100 * SCENE1_PFO_PARENT_TABLE_STRIDE];
    memcpy(dst, chunk, bytes);
}

#ifdef _WIN32

/* Load one effect%d.dat file via disk-first / storage-fallback (same
 * shape as scene1_overlay_table_load).  On success, dispatch its
 * parent template chunk to scene1_pfo_parent_table_load_chunk.
 * Returns 1 on a successful read, 0 on lookup miss / I/O error. */
static int pfo_load_one_file(const char *name, int file_idx)
{
    if (!name) return 0;

    uint8_t *buf = NULL;
    size_t   buf_len = 0;

    /* Disk first (engine FUN_005038b0 fopen). */
    FILE *f = fopen(name, "rb");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long sz = ftell(f);
            if (sz >= 0 && fseek(f, 0, SEEK_SET) == 0) {
                buf = (uint8_t *)malloc((size_t)sz + 10);
                if (buf) {
                    size_t n = fread(buf, 1, (size_t)sz, f);
                    buf_len = n;
                }
            }
        }
        fclose(f);
    }

    /* Storage fallback (engine FUN_00434585 + FUN_004346bf). */
    if (!buf) {
        size_t sz = storage_get_size(name);
        if (sz == 0) return 0;
        buf = (uint8_t *)malloc(sz + 10);
        if (!buf) return 0;
        size_t n = storage_read(name, buf);
        if (n == 0) {
            free(buf);
            return 0;
        }
        buf_len = n;
    }

    /* The "secondary-table chunk" (bytes 0..0x4330) is the 2D-overlay
     * particle TEMPLATE table (engine DAT_00733820).  ALL FOUR sets feed
     * the one contiguous table (fread at DAT_00733820 + file_idx·0x4330)
     * the spawner FUN_00414345 indexes via DAT_00733884 — set 0 carries
     * template 0x3b (`目玉商品`, the shop-display sparkle), set 1 the sale
     * coin-shower templates 170-176 (RE §21.31). */
    if (buf_len >= PFO_SECONDARY_CHUNK_BYTES) {
        scene1_overlay_templates_load_chunk_at(file_idx, buf,
                                               PFO_SECONDARY_CHUNK_BYTES);
    }

    /* Skip the secondary-table chunk; pass the parent-template chunk
     * onwards.  If the file is shorter than the secondary chunk size,
     * there's no parent data to load — leave defaults intact. */
    if (buf_len > PFO_SECONDARY_CHUNK_BYTES) {
        scene1_pfo_parent_table_load_chunk(
            file_idx,
            buf + PFO_SECONDARY_CHUNK_BYTES,
            buf_len - PFO_SECONDARY_CHUNK_BYTES);
    }
    free(buf);
    return 1;
}

int scene1_pfo_parent_table_load_all(void)
{
    /* Engine L17-L42 init runs once before the file loop; mirror that
     * here so missing files leave their slice at canonical defaults. */
    scene1_pfo_parent_table_init();

    static const char *const files[PFO_PARENT_FILE_COUNT] = {
        "ef/effect1.dat",
        "ef/effect2.dat",
        "ef/effect3.dat",
        "ef/effect4.dat",
    };
    int loaded = 0;
    for (int i = 0; i < PFO_PARENT_FILE_COUNT; i++) {
        if (pfo_load_one_file(files[i], i)) loaded++;
    }
    return loaded;
}

#endif /* _WIN32 */
