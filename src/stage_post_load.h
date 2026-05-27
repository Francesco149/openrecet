/*
 * stage_post_load.h — port of engine FUN_00435c98 @ 0x435c98 (309 B).
 *
 * Post-load stage init.  Engine fires this from FUN_0049a59e at two
 * sites:
 *   L100602 — at the title-fade kick (counter == 0x1e), before fade-in
 *             completes.  Forces stage index to 0 and runs the body.
 *   L100757 — inside the NEW GAME branch (DAT_0438bed4 != 0) of the
 *             post-fade commit block, right after FUN_0049001c
 *             (= save_bank_init_one).
 * Also called from non-title paths (dungeon entry, etc.) when a stage
 * change commits.  For the current chip only the L100757 wire matters
 * (matches the retail frame-59 NEW GAME burst); other call sites land
 * as their owning scenes port.
 *
 * Body (decomp at docs/decompiled/all.c L33102-33159):
 *   1. Call FUN_004844ef (stat aggregator — STUB).
 *   2. Reset scratch [DAT_056da1cc..d4] = (0, 3, 1).
 *   3. (Engine calls FUN_0047a8c0 = apply_chara_interp here — see
 *      "apply_chara_interp note" below for why this chip skips it.)
 *   4. Compute scratch positions DAT_056db0bc / DAT_056db0c0 from
 *      chara record bytes [+0x3c..+0x42] (HP/SP-interp leaks into
 *      these as the "position" floats — see chip notes), then mirror
 *      into the backup pair DAT_056db0c4 / DAT_056db0c8.
 *   5. Call xp_curve_threshold(level) and xp_curve_threshold(level+1)
 *      → chara record dwords [0x12] / [0x13] (xp_curr / xp_next).
 *   6. Clamp chara record dword [0x11] (xp_value) into the
 *      [xp_curr, xp_next] window.
 *   7. Read per-stage int from `stage_record + 0x2c3f4` → float
 *      DAT_0438b91c.  Stage record isn't ported → defaults to 0.
 *   8. Call FUN_004360b6 (sibling — STUB).
 *   9. Zero 6 dwords at DAT_056dae44 (to -1), 25 dwords at
 *      DAT_0438b4ec, then the 4x6 grid at DAT_0438bedc / bef4 / bf0c /
 *      bf24 (24 dwords).  Write DAT_0438b4c4 = 1 between the second
 *      and third blocks.
 *  10. Call FUN_00435fbb(1, 0) (full body) and FUN_00435dcd(1, 0) (full
 *      body via stage_post_load_pulse_first_row).
 *
 * Storage layout:
 *   The engine's stage record is a 0x2dfc8-byte block per stage.  Most
 *   of it isn't ported.  This module owns the chara record sub-array
 *   (8 chara records × 27 dwords each = 864 bytes) and the scratch
 *   globals listed in step 9.  When a future chip lifts the full
 *   stage record, fields fold back into a single struct.
 *
 * Active stage / chara index:
 *   Engine: DAT_0438b1e0 (stage) and DAT_0438b7d8 (chara).  Both are
 *   BSS-zero on NEW GAME → this chip hardcodes (stage=0, chara=0).
 *   When stage-change subsystem ports, lift these to module globals.
 *
 * apply_chara_interp note:
 *   Engine L33118 calls FUN_0047a8c0(stage_record) — the chara stat
 *   interpolator.  The port currently fires apply_chara_interp once,
 *   via save_bank_init_one(0)'s internal call.  Re-invoking it here
 *   would require either (a) lifting save_bank.c's static
 *   apply_chara_interp into a public API + allocating a full
 *   0x2dfc8-byte stage-record buffer (188 KB) so its
 *   `bank_bytes + 0x2cee0` offset arithmetic lands on our chara
 *   record block, or (b) re-implementing the interpolator against
 *   g_stage_chara directly.  Both deferred: no port consumer reads
 *   our g_stage_chara HP/SP fields today, so the missed call has no
 *   observable state divergence.  Documented as a known call-count
 *   divergence on top of the existing 1-vs-9 collapse for
 *   apply_chara_interp.
 *
 * Wiring:
 *   scene.c::scene_post_fade_init() calls stage_post_load_init()
 *   right after save_bank_init_one(0) (matches engine L100757).
 *
 * Pure C, no Win32 surface.  Unit-testable.
 */
#ifndef OPENRECET_STAGE_POST_LOAD_H
#define OPENRECET_STAGE_POST_LOAD_H

#include <stdint.h>

/* ─── per-chara record (engine layout, 0x6c bytes per chara) ────────────
 *
 * Field offsets we read/write in this chip (others stay BSS-zero):
 *
 *   +0x00  int32  level                       (dword [0])
 *   +0x3c  int16  pos_x_lo  (== HP_interp after apply_chara_interp)
 *   +0x3e  int16  pos_x_hi
 *   +0x40  int16  pos_y_lo  (== SP_interp after apply_chara_interp)
 *   +0x42  int16  pos_y_hi
 *   +0x44  int32  xp_value                    (dword [0x11])
 *   +0x48  int32  xp_threshold_current        (dword [0x12])
 *   +0x4c  int32  xp_threshold_next           (dword [0x13])
 */
#define STAGE_POST_LOAD_CHARA_COUNT     8
#define STAGE_POST_LOAD_CHARA_DWORDS    0x1b   /* 27 dwords = 108 bytes */
#define STAGE_POST_LOAD_CHARA_BYTES     0x6c

/* ─── public entry points ─────────────────────────────────────────────── */

/* FUN_00435c98 @ 0x435c98 — runs the post-load init body.  Idempotent. */
void stage_post_load_init(void);

/* FUN_00435fbb @ 0x435fbb — 5-fold counter-driven scratch pulse.
 *
 *   reset_arrays: 1 to zero DAT_0438bef4[0..4] and DAT_0438bf24[0..4]
 *                 before the main loop; 0 to skip the reset (preserves
 *                 the per-index counter accumulated across prior calls).
 *   reset_counter_idx: when >= 0 (and < 5), forces
 *                 DAT_0438bf24[reset_counter_idx] = 0 before the loop.
 *                 -1 (sentinel) skips this targeted reset.
 *
 * After the loop, all five counters are incremented by 1.
 *
 * Engine call sites:
 *   FUN_00435c98 L33157            — (1, 0): full reset, idx 0 forced.
 *   FUN_0048fe43 (and three others) — (0, *): preserve counters,
 *                                     pulse one index per call. */
void stage_post_load_pulse_5fold(int reset_arrays, int reset_counter_idx);

/* FUN_00435dcd @ 0x435dcd — first-row mode-dispatched weight write
 * + 6-fold counter pulse.
 *
 *   reset_arrays: 1 zeros DAT_0438bedc[0..5] AND DAT_0438bf0c[0..5]
 *                 before the dispatch.  Engine's stage_post_load_init
 *                 (FUN_00435c98) passes 1.  0 preserves both — used by
 *                 four tick-time callers (FUN_004844ef and three other
 *                 dungeon-step paths) to pulse one slot per call.
 *   force_clear_idx: when >= 0 (and < 6), zeros bedc[force_clear_idx]
 *                 before the dispatch (post the array reset).  Engine's
 *                 stage_post_load_init passes 0; tick-time callers pass -1.
 *
 * Behavior summary (decomp L33163-L33250):
 *   1. Optionally reset bedc[0..5] and bf0c[0..5] to 0.
 *   2. Optionally force-clear one bedc slot.
 *   3. Pick mode based on g_dat_0438b4d0 (with a deep-dungeon override
 *      for (-1, dungeon_id==5, next_floor>0x1d) → mode 4).
 *   4. Write a mode-specific carve-up into bedc[]:
 *        mode 0: bedc[0] = 1.0
 *        mode 1: bedc[1] = clamp01_high( (bf0c[1]-2)*0.02, 0.2 );
 *                bedc[0] = 1.0 - sum(bedc[1..4])
 *        mode 2..5: pick slot ∈ {0,1,2,3}; carve bedc[slot..slot+2] s.t.
 *                bedc[slot+1] = clamp_high(bf0c[slot+1]*0.04, 0.2)
 *                bedc[slot+2] = clamp_high((bf0c[slot+2]-3)*0.005, 0.05)
 *                bedc[slot]   = 1.0 - bedc[slot+1] - bedc[slot+2]
 *   5. Increment bf0c[0..5] by 1, or by 5 on the rng-gated equipped-item
 *      predicate path (only reachable from tick-time callers via the
 *      reset_arrays==0 branch).
 *
 * On NEW GAME (g_dat_0438b4d0 BSS-zero, bf0c BSS-zero): bedc[0] = 1.0,
 * all other bedc slots stay 0, bf0c[0..5] all increment from 0 → 1.
 *
 * Storage note: bedc[] holds float bit-patterns but the backing store
 * is int32_t (shared with stage_post_load_init's zero pass); memcpy
 * mediates the float view to keep strict-aliasing clean. */
void stage_post_load_pulse_first_row(int reset_arrays, int force_clear_idx);

/* g_dat_0438b4d0 — primary mode selector for the first-row dispatch.
 * Future stage-change subsystem wires this; until then, test-only. */
void    stage_post_load_set_mode_b4d0(int32_t mode);
int32_t stage_post_load_get_mode_b4d0(void);

/* ─── chara record access (test helpers + future consumers) ───────────── */

int32_t stage_post_load_chara_field(int chara_idx, int dword_idx);
void    stage_post_load_set_chara_field(int chara_idx, int dword_idx,
                                        int32_t value);
int16_t stage_post_load_get_chara_short(int chara_idx, int byte_off);
void    stage_post_load_set_chara_short(int chara_idx, int byte_off,
                                        int16_t value);

/* ─── scratch global accessors (test-only today) ──────────────────────── */

int32_t stage_post_load_get_dat_056da1cc(void);
int32_t stage_post_load_get_dat_056da1d0(void);
int32_t stage_post_load_get_dat_056da1d4(void);
int32_t stage_post_load_get_dat_056db0d8(void);
float   stage_post_load_get_dat_056db0bc(void);
float   stage_post_load_get_dat_056db0c0(void);
float   stage_post_load_get_dat_056db0c4(void);
float   stage_post_load_get_dat_056db0c8(void);
int32_t stage_post_load_get_dat_0438bea0(void);
float   stage_post_load_get_dat_0438b91c(void);
int32_t stage_post_load_get_dat_0438b4c4(void);
int32_t stage_post_load_get_dat_056dae44(int idx);    /* idx in [0..5] */
int32_t stage_post_load_get_dat_0438b4ec(int idx);    /* idx in [0..24] */
int32_t stage_post_load_get_dat_0438bedc(int idx);    /* idx in [0..5] */
int32_t stage_post_load_get_dat_0438bef4(int idx);    /* idx in [0..5] */
int32_t stage_post_load_get_dat_0438bf0c(int idx);    /* idx in [0..5] */
int32_t stage_post_load_get_dat_0438bf24(int idx);    /* idx in [0..5] */

/* FUN_00435fbb writes float bit-patterns into bef4[].  This accessor
 * returns the float view via memcpy (strict-aliasing safe). */
float   stage_post_load_get_dat_0438bef4_as_float(int idx);  /* idx in [0..5] */

/* FUN_00435dcd writes float bit-patterns into bedc[].  Float view, same
 * strict-aliasing-safe pattern. */
float   stage_post_load_get_dat_0438bedc_as_float(int idx);  /* idx in [0..5] */

/* Reset all stage_post_load state to BSS-zero.  Test-only helper. */
void stage_post_load_reset_for_test(void);

#endif /* OPENRECET_STAGE_POST_LOAD_H */
