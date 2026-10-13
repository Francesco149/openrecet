/*
 * x87.h — flag-independent x87 precision primitives for retail-exact float code.
 *
 * Retail (MSVC6) runs with x87 CW 0x027F (53-bit, measured in-game under rhost).
 * It keeps intermediates in ST0 and rounds ONLY where it spills to memory
 * (fstp m32 / fstp m64); its constants are m32 (or m64) operands; __ftol
 * truncates the unrounded ST0.  GCC on x87 does neither by default:
 *   -std=c11  (-fexcess-precision=standard): `0.1f` literals evaluate as long
 *             double; every float assignment/parameter rounds.
 *   -std=gnu11 (=fast): float literals keep their float value, but float casts
 *             and assignments may NOT round (kept in registers).
 * Neither matches retail in general (P1 oracle, 2026-10-13:
 * tools/oracle/p1_haggle_offers.c — c11 ~1% / gnu11 ~0.05% mismatches).
 *
 * Rule when porting float math: read the retail DISASSEMBLY (not a decompile)
 * and mirror it:
 *   ST0 chain        → long double expression
 *   fstp m32 / m64   → spill32() / spill64()  (volatile round-trip: always rounds)
 *   fmul/fadd m32 K  → a `static const float` K (an object, so it is always the
 *                      m32 value — never a widened literal)
 *   __ftol           → x87_ftol() of the long double value
 * Code written this way computes identically under c11 and gnu11, at any CW.
 * Verify with a P1 oracle (tools/oracle/) or an rhost shadow hook.
 */
#ifndef OPENRECET_X87_H
#define OPENRECET_X87_H
#include <stdint.h>

static inline int32_t x87_ftol(long double v) { return (int32_t)v; }               /* __ftol */
static inline float  spill32(long double x) { volatile float  v = (float)x;  return v; }  /* fstp m32 */
static inline double spill64(long double x) { volatile double v = (double)x; return v; }  /* fstp m64 */

#endif
