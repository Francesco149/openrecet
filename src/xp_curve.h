/*
 * xp_curve.h — port of engine FUN_0048a331 @ 0x48a331 (23 B).
 *
 * Pure quadratic XP threshold curve:
 *
 *   xp_threshold(level) = level * (level + 1) * 150     (level > 0)
 *                       = 0                              (level == 0)
 *
 * Called from two sites in the engine:
 *
 *   (1) FUN_00435c98 (post-new-game stage init):
 *       Twice in sequence — `f(level)` and `f(level + 1)` — to
 *       populate a chara record's [xp_at_current_level,
 *       xp_at_next_level] pair (record offsets +0x48 / +0x4c, aka
 *       int* +0x12 / +0x13).  At NEW GAME, level is 0 → both writes
 *       are 0 and 300.
 *
 *   (2) FUN_0048a383 (per-frame level-up handler in the stat-bump
 *       cluster — unported in this chip):
 *       On a level bump from N to N+1: xp_curr <- old xp_next, then
 *       xp_next <- f(N+2).
 *
 * Multiplier `0x96` (= 150) is the engine's hard-coded XP base.
 * The level cap from the level-up handler is `level < 0x62` (98),
 * not enforced inside the curve itself — the curve is pure.
 *
 * Pure C, no Win32 surface, no globals.  Trivially testable.
 */

#ifndef OPENRECET_XP_CURVE_H
#define OPENRECET_XP_CURVE_H

#include <stdint.h>

/* FUN_0048a331 — XP threshold for a given level.
 *
 * Returns 0 for level == 0 (engine guard), else level*(level+1)*150.
 * Engine input type is `int` (signed); negative levels follow the
 * pure formula (which yields a positive product since both factors
 * share a sign), but no caller passes negative values.
 */
int32_t xp_curve_threshold(int32_t level);

#endif /* OPENRECET_XP_CURVE_H */
