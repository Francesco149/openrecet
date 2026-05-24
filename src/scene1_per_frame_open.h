/*
 * scene1_per_frame_open.h — per-frame open of the scene-1 particle
 * integrator (engine FUN_00414929, called at L1 of FUN_0040fb3a).
 *
 * The full FUN_00414929 ticks two unrelated entity tables before the
 * particle integrator runs its per-type handlers.  This header lands:
 *
 *  - Table A (PFO.1): a 256-slot spawn-request queue at engine
 *    `DAT_00730c20` (sentinel field aliased at `DAT_00730c30`).
 *
 *  - Parent template table (PFO.2): the 400-slot × 0x5f-dw table at
 *    `DAT_007444e0` that Table A's tick walks 7 sub-records of per
 *    live slot.  Populated at boot by `FUN_0041276e` parsing
 *    `ef/effect%d.dat` (PFO.7).
 *
 * The Table B half (overlay slots) is already covered by
 * `scene1_overlay.{c,h}` (chip O.2's slot table).
 *
 * Chip PFO.2 (this header): adds typed storage + default-fill init for
 * the parent template table.  The init is NOT wired into any caller
 * yet — it stands ready for PFO.7 (the binary-file parser) to call
 * before populating real per-record values.  Storage stays BSS-zero
 * until either init or parser runs.  The tick body, the allocators
 * FUN_004132c1 / FUN_0041331d, and the parser land in PFO.3..PFO.7.
 * See `docs/findings/scene1-per-frame-open.md` for the full ladder.
 *
 * Dormant in HOUSE today — Table A is sentinel-empty (PFO.1 init), so
 * the parent template walk inside Table A's "if slot live" branch is
 * unreachable.  All four ladders (PFO.1..PFO.5) keep goldens
 * bit-exact.
 */
#ifndef SCENE1_PER_FRAME_OPEN_H
#define SCENE1_PER_FRAME_OPEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENE1_PFO_TABLE_A_COUNT   256
#define SCENE1_PFO_TABLE_A_STRIDE  11   /* dw count; 44 B per slot */

/*
 * Slot layout (dw indices within an 11-dw slot).  The engine's tick
 * indexes off the sentinel field at +4 dw (= `DAT_00730c30`-aliased
 * pointer); we store the slot as a contiguous 11-dw array starting
 * at dw 0.
 *
 * Field semantics from the allocator FUN_004132c1 (10-arg pose-style)
 * and FUN_0041331d (9-arg explicit).  See survey doc for tick-side
 * reads.
 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM0   0   /* DAT_00730c20 — entry base; allocator-set */
#define SCENE1_PFO_TABLE_A_OFF_PARAM1   1   /* DAT_00730c24 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM2   2   /* DAT_00730c28 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM3   3   /* DAT_00730c2c — FUN_004132c1 sets to -520.0f */
#define SCENE1_PFO_TABLE_A_OFF_SENTINEL 4   /* DAT_00730c30 — TYPE / parent-template id; -1 = empty */
#define SCENE1_PFO_TABLE_A_OFF_PARAM5   5   /* DAT_00730c34 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM6   6   /* DAT_00730c38 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM7   7   /* DAT_00730c3c */
#define SCENE1_PFO_TABLE_A_OFF_PARAM8   8   /* DAT_00730c40 */
#define SCENE1_PFO_TABLE_A_OFF_AGE      9   /* DAT_00730c44 — incremented per tick; kill at 300 */
#define SCENE1_PFO_TABLE_A_OFF_MODE    10   /* DAT_00730c48 — FUN_004132c1 sets to 1, FUN_0041331d sets to 0 */

extern int32_t g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_COUNT *
                                    SCENE1_PFO_TABLE_A_STRIDE];

/*
 * Engine FUN_00414902 Table A half (L12548-L12552): set every slot's
 * SENTINEL field to -1.  Other fields untouched (engine doesn't zero
 * them either).  Called from FUN_0040f64b on every scene-1 entry —
 * wired into `scene1_records_reset()` to match.
 */
void scene1_pfo_table_a_init(void);

/* ------------------------------------------------------------------
 * Parent template table (PFO.2) — engine `DAT_007444e0`
 * ------------------------------------------------------------------
 *
 * 400 entries × 0x5f dw (= 95 dw / 380 B / 0x17c B) = 38000 B per file
 * × 4 files (`ef/effect{1..4}.dat`) = 152000 B (0x251c0) total.
 *
 * Per-entry layout (in 4-byte dw indices relative to entry start):
 *
 *   dw 0..24  : preamble — name string at dw 0 + unidentified extras.
 *               Engine init writes "<unknown>" via FUN_005038ff at
 *               entry+0.  The tick (FUN_00414929) does NOT read this
 *               region; PFO.7 parser overwrites from file.
 *
 *   dw 25..31 : sub_rec[0..6].sentinel — struct-of-arrays.  Engine
 *               init: -1 in every slot.  Tick reads via
 *               `piVar1 = &DAT_00744544 + entry_id * 0x5f`
 *               (= entry+25 dw); gate `*piVar1 != -1`.
 *
 *   dw 32..38 : sub_rec[0..6].age_match — engine init: 0.  Tick reads
 *               `piVar1[7]` per sub-record (so dw 32+k for sub k);
 *               must equal Table A entry's age (`piVar2[5]`) for the
 *               sub-record to fire.
 *
 *   dw 39..66 : sub_rec[0..6].rgba_ints — 4 dw per sub-record (28 dw
 *               total).  Engine init: 100/100/100/100 per sub-record.
 *               NOT consumed by the tick — looks like a per-sub-record
 *               color tint forwarded to the spawn via different
 *               consumer.
 *
 *   dw 67..73 : sub_rec[0..6].scale_mul — engine init: 1.0f.  Tick
 *               reads `piVar1[0x2a]` per sub-record (= dw 67+k);
 *               multiplies Table A's scale.
 *
 *   dw 74..94 : sub_rec[0..6].xyz — 3 dw per sub-record (21 dw total).
 *               Engine init: 0 each.  Tick reads via
 *               `pfVar3 = &DAT_0074460c + entry_id * 0x5f`
 *               (= entry+75 dw), then `pfVar3[-1..1]` per sub-record
 *               (sub k spans dw 74+3k .. 76+3k); xyz offsets added
 *               to Table A pos at spawn time.
 *
 * Source: FUN_00412a89 L17-L42 (first init loop) + FUN_00414929
 * L28-L55 (tick reads).
 */
#define SCENE1_PFO_PARENT_TABLE_COUNT          400
#define SCENE1_PFO_PARENT_TABLE_STRIDE         95  /* dw count; 0x5f */

#define SCENE1_PFO_PARENT_TABLE_SUB_COUNT      7   /* sub-records per entry */

/* Per-entry dw offsets to each sub-record block start (sub k is at
 * BLOCK + k for 1-dw fields, BLOCK + k*4 / BLOCK + k*3 for arrays). */
#define SCENE1_PFO_PARENT_OFF_NAME             0   /* dw 0 — char name[]; 25 dw of preamble */
#define SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0   25  /* sub_rec[k].sentinel  = dw 25+k */
#define SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0  32  /* sub_rec[k].age_match = dw 32+k */
#define SCENE1_PFO_PARENT_OFF_SUB_RGBA_0       39  /* sub_rec[k].rgba_ints = dw 39+k*4 .. 42+k*4 */
#define SCENE1_PFO_PARENT_OFF_SUB_SCALE_MUL_0  67  /* sub_rec[k].scale_mul = dw 67+k */
#define SCENE1_PFO_PARENT_OFF_SUB_XYZ_0        74  /* sub_rec[k].xyz       = dw 74+k*3 .. 76+k*3 */

extern int32_t g_scene1_pfo_parent_table[SCENE1_PFO_PARENT_TABLE_COUNT *
                                         SCENE1_PFO_PARENT_TABLE_STRIDE];

/*
 * Mirrors the FIRST init loop of engine FUN_00412a89 (L17-L42).  For
 * every entry, sets the 7 sub-record blocks to engine defaults:
 *
 *   sub_rec[k].sentinel  = -1
 *   sub_rec[k].age_match = 0
 *   sub_rec[k].rgba_ints = (100, 100, 100, 100)
 *   sub_rec[k].scale_mul = 1.0f
 *   sub_rec[k].xyz       = (0, 0, 0)
 *
 * Does NOT touch the dw 0..24 preamble (the name field gets
 * "<unknown>" from FUN_005038ff in the engine; we leave it BSS-zero
 * since the tick doesn't read it and PFO.7's parser will overwrite).
 *
 * Idempotent.  Not wired into any caller in PFO.2 — the parser
 * (PFO.7) is the natural caller, after which it overwrites with
 * per-record values from `ef/effect%d.dat`.
 */
void scene1_pfo_parent_table_init(void);

/* ------------------------------------------------------------------
 * Table B per-tick body (PFO.3) — engine FUN_00414929 L67-L195
 * ------------------------------------------------------------------
 *
 * Iterates all 4096 `g_scene1_overlay_slots`, skipping any with
 * ACTIVE == -1.  For each live slot, runs:
 *
 *   1. Anim-cell tick (not type-gated; runs even for AGE < 0).
 *      Increments OFF_ANIM_FRAME_COUNTER every call; when it hits the
 *      shape entry's FRAME_PERIOD, reset to 0 and bump
 *      OFF_ANIM_CELL_INDEX.  Cell index clamps or wraps at FRAME_COUNT
 *      based on the shape's LOOP_MODE.
 *
 *   2. Type-dispatched integrator (only when AGE >= 0):
 *        SHAPE_MODE == 1     : accum (VEL_X/Y/Z) += vel (BEND_X/Y/Z);
 *                              pos = POS_COPY + matrix(OWNER_A) + accum.
 *        SHAPE_MODE == 6     : same as 1 but matrix is at OWNER_B+0x3f0.
 *        TYPE_SHAPE in 8/9/10: ROT_Y += BEND_Y; no pos update.
 *        otherwise           : pos += BEND.
 *
 *      Note: the tick's "vel" is OFF_BEND_X/Y/Z (renderer name) and the
 *      tick's "accum" is OFF_VEL_X/Y/Z.  See scene1_overlay.h field
 *      offsets for the renderer/tick perspective mismatch.
 *
 *      Then: drag (BEND *= TEMPLATE5_COPY); gravity (BEND_Y += UNK_48);
 *      energy decay (SCALE_X += TEMPLATE11_COPY).
 *
 *      SKIPPED for PFO.3 (PFO.4 lands it): the SHAPE_MODE==4 + UNK_48!=0
 *      "shop walker" aim-toward-(11, -9, -520) physics body + 50% kill
 *      at terminal velocity.  Dormant in HOUSE since no spawner
 *      populates type-4 slots with non-zero UNK_48.
 *
 *   3. AGE++ and age-kill check (always run, even for AGE < 0):
 *      Kill (set ACTIVE = -1) when FADE_OUT_OFFSET != -1 AND
 *      (FADE_OUT_OFFSET <= AGE+1 - AGE_BIRTH OR SCALE_X <= 0).
 *      Kill is bypassed when SHAPE_MODE==4 && UNK_48!=0 (the
 *      shop-walker body handles its own kill via random half-kill).
 *
 * NOT wired into any caller in PFO.3 — PFO.5 lands the wiring of
 * FUN_00414929 (Table A + this) into `particles_per_frame_open` in
 * scene1_particles_tick.c.  Until then, overlay slots stay
 * sentinel-empty in HOUSE (PFO.2.1's reset wiring) and this tick is
 * unreachable in production.
 */
void scene1_pfo_table_b_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_PER_FRAME_OPEN_H */
