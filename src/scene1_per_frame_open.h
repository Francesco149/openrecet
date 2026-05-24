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
 * Table B per-tick body (PFO.3 + PFO.4) — engine FUN_00414929 L67-L195
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
 *      Then: drag (BEND *= TEMPLATE5_COPY); gravity (BEND_Y += UNK_48).
 *
 *      PFO.4: SHAPE_MODE==4 + UNK_48!=0 "shop walker" aim physics body.
 *      Gated on AGE > 30 + (slot_idx % 4) — per-slot stagger so 4
 *      type-4 spawns the same frame don't move in lockstep.  Target is
 *      `(11.0*factor, -9.0*factor, -520.0)` with `factor` always 1.2
 *      (formula `(AGE-30)*0.4 + 1.2` clamps at 1.2 max — see engine
 *      quirk #50).  Per-tick step: UNK_48 *= 0.8 (decay), BEND +=
 *      normalize(target-pos)*0.1, then AGE>40 → BEND *= max(0.97,
 *      1.0 - (AGE-40)*0.002), then |BEND|>1 → normalize to unit,
 *      then pos.y < target.y → BEND_Y -= UNK_48 (cancel gravity
 *      overshoot).  Terminal kill check: if |target-pos|<0.5 OR pos.y <
 *      target.y → ACTIVE = -1 and fire the type-4 kill hook (engine
 *      calls FUN_0040656e which plays SE 0x29d "thunk" + sets screen
 *      shake counter DAT_00648280 = 4).  Energy decay (SCALE_X +=
 *      TEMPLATE11_COPY) follows.
 *
 *   3. AGE++ and age-kill check (always run, even for AGE < 0):
 *      Kill (set ACTIVE = -1) when FADE_OUT_OFFSET != -1 AND
 *      (FADE_OUT_OFFSET <= AGE+1 - AGE_BIRTH OR SCALE_X <= 0).
 *      Kill is bypassed when SHAPE_MODE==4 && UNK_48!=0 (the
 *      shop-walker body handles its own kill via terminal check above).
 *
 * NOT wired into any caller in PFO.3 — PFO.5 lands the wiring of
 * FUN_00414929 (Table A + this) into `particles_per_frame_open` in
 * scene1_particles_tick.c.  Until then, overlay slots stay
 * sentinel-empty in HOUSE (PFO.2.1's reset wiring) and this tick is
 * unreachable in production.
 */
void scene1_pfo_table_b_tick(void);

/* PFO.4 terminal-kill hook.  Fires once per slot when the type-4
 * shop-walker body kills the slot (either via |target-pos|<0.5 or
 * pos.y<target.y).  Argument is the slot index (0..4095).
 *
 * The engine's equivalent is `FUN_0040656e` which sets the screen-shake
 * counter `DAT_00648280 = 4` and calls `FUN_00499519(0x29d)` (= play
 * SE id 0x29d "thunk").  Neither of those side-effects is wired in any
 * port today: DAT_00648280 has no other ported reader/writer, and SE
 * 0x29d's player has no consumer either.  Win32 builds can wire a hook
 * that calls audio_play_se_by_id(0x29d) once the rest of the pipeline
 * lands; for now the default is no-op.  Tests install the hook to
 * observe firings.
 *
 * Pure-C — host-linkable. */
void scene1_pfo_set_type_4_terminal_kill_hook(void (*hook)(int slot_idx));
void scene1_pfo_clear_type_4_terminal_kill_hook(void);

/* Internal — called by scene1_pfo_table_b_tick when the type-4 body
 * triggers terminal kill.  Exposed for direct test use. */
void scene1_pfo_fire_type_4_terminal_kill(int slot_idx);

/* ------------------------------------------------------------------
 * Table A per-tick body (PFO.5a) — engine FUN_00414929 L1-L43
 * ------------------------------------------------------------------
 *
 * Walks all 256 g_scene1_pfo_table_a slots.  For each live slot
 * (SENTINEL != -1), runs an inner 7-iter walk over the parent template
 * table entry indexed by SENTINEL.  Each sub-record fires an
 * scene1_overlay_spawn call iff:
 *
 *     sub_rec[k].sentinel  != -1  AND
 *     sub_rec[k].age_match == slot.AGE
 *
 * After the inner walk, age is incremented; at age == 300 the slot is
 * self-cleared (SENTINEL = -1).
 *
 * The spawn call uses one of two arg-construction modes selected by
 * slot.MODE (dw 10):
 *
 *   MODE == 0 — "passthrough":
 *     spawn(template_owner = slot[0],
 *           pos_x = slot[1] + sub_rec.xyz_x,
 *           pos_y = slot[2] + sub_rec.xyz_y,
 *           pos_z = slot[3] + sub_rec.xyz_z + alt_offset,
 *           template_id = sub_rec.sentinel,
 *           scale_base  = slot[5] * sub_rec.scale_mul,
 *           override_dur = slot[6],
 *           override_rot_y = slot[7]  (as float — bit-pattern passed),
 *           shape_mode = slot[10] (= 0 in this branch),
 *           mode       = 0)
 *
 *   MODE != 0 — "projected":
 *     spawn(template_owner = 0,
 *           pos_x = 16.5 - (slot[1] + sub_rec.xyz_x) / 19.5,
 *           pos_y = 12.4 - (slot[2] + sub_rec.xyz_y) / 19.5,
 *           pos_z = -520.0,
 *           template_id = sub_rec.sentinel,
 *           scale_base  = slot[5] * sub_rec.scale_mul,
 *           override_dur = slot[6],
 *           override_rot_y = 0.0f,
 *           shape_mode = slot[10],
 *           mode       = 1)
 *
 * `alt_offset` is 0 by default, -520 when g_scene1_pfo_alt_mode != 0
 * (= engine `DAT_074b2ee4`; PHC #17 stand-in).
 *
 * NOT yet reachable in production: PFO.6's allocators (FUN_004132c1 +
 * FUN_0041331d) populate Table A but haven't ported; PFO.1's init keeps
 * every slot's SENTINEL at -1 in HOUSE, so the body is dormant.
 */
void scene1_pfo_table_a_tick(void);

/* PHC #17 stand-in for engine `DAT_074b2ee4`.  No writers in the
 * decompile dump; hypothesized per-stage "alt projection" flag set by
 * unported per-stage init.  When nonzero, the passthrough-mode spawn
 * arm adds -520 to pos_z (effectively forcing the spawn to a fixed
 * near-plane Z).  HOUSE default 0.  Exposed for tests + future stage
 * init writers. */
extern int32_t g_scene1_pfo_alt_mode;

/* Spawn-call interception for Table A's tick body.  Default behavior
 * (no hook installed): scene1_pfo_table_a_tick calls
 * scene1_overlay_spawn directly with the 10 args the engine pushes.
 * With a hook installed: the hook is called INSTEAD of
 * scene1_overlay_spawn — useful for tests that want to observe the
 * spawn-arg construction without setting up a full template table /
 * shape table state.  Production paths leave the hook NULL. */
typedef void (*scene1_pfo_spawn_hook_fn)(const void *template_owner,
                                         float pos_x, float pos_y, float pos_z,
                                         int   template_id,
                                         float scale_base,
                                         int   override_dur,
                                         int   override_rot_y,
                                         int   shape_mode,
                                         int   mode);
void scene1_pfo_set_spawn_hook(scene1_pfo_spawn_hook_fn hook);
void scene1_pfo_clear_spawn_hook(void);

/* ------------------------------------------------------------------
 * Table A allocators (PFO.6) — engine FUN_004132c1 + FUN_0041331d
 * ------------------------------------------------------------------
 *
 * Both allocators do a linear scan of Table A for the first slot with
 * SENTINEL == -1, then populate it with the caller's args + a few
 * fixed constants.  When the table is full (no -1 slot), the call is
 * a no-op (engine returns without writing).  Per-call writes always
 * include AGE = 0; the two functions differ only in field-fill
 * pattern + MODE flag.
 *
 * **scene1_pfo_table_a_alloc_projected** — engine FUN_004132c1 (6 args,
 * 92 bytes).  Writes:
 *
 *   slot[PARAM0]   = 0
 *   slot[PARAM1]   = pos_x       (float bits)
 *   slot[PARAM2]   = pos_y       (float bits)
 *   slot[PARAM3]   = -520.0f     (= engine .rdata 0xc4020000)
 *   slot[SENTINEL] = template_id (a non-negative int — gates the slot)
 *   slot[PARAM5]   = scale_base  (float bits)
 *   slot[PARAM6]   = override_dur
 *   slot[PARAM7]   = 0           (= 0.0f bits)
 *   slot[PARAM8]   = param_8
 *   slot[AGE]      = 0
 *   slot[MODE]     = 1           (= projected → tick uses 16.5/12.4
 *                                  projection branch)
 *
 * Called by engine state writers we haven't ported yet; never NULL-
 * checked.  Race-free in the engine (single-threaded sim).
 *
 * **scene1_pfo_table_a_alloc_passthrough** — engine FUN_0041331d (9 args,
 * 89 bytes).  Writes ALL 9 slot fields directly from the args (no
 * fixed constants apart from AGE=0 + MODE=0):
 *
 *   slot[PARAM0]   = template_owner (an int treated as a pointer by
 *                                    the spawn API at tick time)
 *   slot[PARAM1]   = pos_x       (float bits)
 *   slot[PARAM2]   = pos_y       (float bits)
 *   slot[PARAM3]   = pos_z       (float bits)
 *   slot[SENTINEL] = template_id
 *   slot[PARAM5]   = scale_base  (float bits)
 *   slot[PARAM6]   = override_dur
 *   slot[PARAM7]   = override_rot_y_bits (engine fld+fstp — float bits)
 *   slot[PARAM8]   = param_8
 *   slot[AGE]      = 0
 *   slot[MODE]     = 0           (= passthrough → tick uses
 *                                  slot + sub.xyz addition)
 *
 * Both: no caller in the current port.  PFO.6 lands the allocators but
 * leaves Table A sentinel-empty in HOUSE until a real consumer ports.
 *
 * Asm verified at 0x4132c1..0x413315 (projected) and 0x41331d..0x413375
 * (passthrough).  Ghidra's decomp accurately reflects the field writes;
 * `param_8` is the slot[8] dw, never read by the tick.
 *
 * To match the engine's no-op-on-full behavior, both allocators return
 * the slot index that was claimed (0..255), or -1 when the table is
 * full.  Engine returns void; we add the return value as an
 * observability aid for tests (and any future consumer that wants to
 * react to allocator failure).
 */
int scene1_pfo_table_a_alloc_projected(float pos_x, float pos_y,
                                       int   template_id,
                                       float scale_base,
                                       int   override_dur,
                                       int   param_8);

int scene1_pfo_table_a_alloc_passthrough(int   template_owner,
                                         float pos_x, float pos_y, float pos_z,
                                         int   template_id,
                                         float scale_base,
                                         int   override_dur,
                                         int   override_rot_y_bits,
                                         int   param_8);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_PER_FRAME_OPEN_H */
