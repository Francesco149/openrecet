/*
 * rng.h — Engine LCG random number generator.
 *
 * Mirrors FUN_005041f6 / FUN_00471089 (RNG) + FUN_0050bcff / FUN_005045eb
 * (time → seed). The engine reimplements MSVC's classic rand() with its
 * own state global (DAT_006023a0) so it doesn't share the CRT's seed —
 * but the LCG constants (0x343fd, 0x269ec3) are bit-identical to MSVC, so
 * a given seed produces the same sequence of values either way.
 *
 * Initial seed at process start: 1 (the .data initial value of DAT_006023a0
 * in the unpacked binary — confirmed at file offset matching that VA).
 * The engine reseeds this from wall-clock time during WinMain step 3
 * (FUN_005045eb → FUN_00471050 → FUN_005041ec); see rng_seed_from_now.
 *
 * WinMain step 2 (FUN_00451790's particle randomization) runs BEFORE the
 * reseed — so those particles are deterministic and identical every boot.
 */

#ifndef OPENRECET_RNG_H
#define OPENRECET_RNG_H

#include <stdint.h>

/* Engine RNG state. Mirrors DAT_006023a0. Initial value 1 matches the
 * unpacked binary's .data section (NOT MSVC's default — the engine writes
 * `1` to its initial-data slot independently). */
extern uint32_t g_rng_seed;

void     rng_seed(uint32_t seed);

/* One LCG step. Returns the 15-bit value the engine exposes via
 * FUN_005041f6 (= classic MSVC rand() result). */
uint16_t rng_next15(void);

/* Returns rng_next15() / 32768.0, matching FUN_00471089. The engine
 * computes this at 80-bit precision then narrows to 32-bit float on
 * store; we use 32-bit throughout (exact for any seed since 32768 is a
 * power of two and rng_next15 returns ≤ 32767). */
float    rng_next_unit(void);

/*
 * Compute the engine's time-derived seed for a given local datetime + DST
 * flag. Mirrors FUN_0050bcff at 0x50bcff exactly:
 *
 *   days  = (year-1900)*365 + day_of_year_offset[month] + day
 *           + ((year-1901) >> 2)               (leap correction)
 *           + (1 if month > 2 and year%4 == 0)
 *   secs  = (((hour + days*24) * 60 + minute) * 60
 *            + 0x7080 + 0x7c558180 + second)
 *   if dst == 1:  secs += -3600
 *
 * The two constants come from the engine's tzset-style table at
 * DAT_006038d0 (timezone offset = PST = 28800 = 0x7080 seconds) and an
 * epoch-adjustment literal (0x7c558180). These are baked-in MSVC CRT
 * tzset defaults; the engine doesn't actually call _tzset, so they
 * remain at their PST defaults regardless of host locale.
 *
 * Returns -1 if year is outside 1970..2038 (matches the engine's range
 * check `year - 1900` in [0x46..0x8a]).
 *
 * dst is `0` (not in DST), `1` (in DST — applies -3600s adjustment), or
 * `-1` (GetTimeZoneInformation failed; engine treats as "no adjustment"
 * via the fall-through after the auto-detect branch).
 */
int32_t  rng_compute_seed(int year, int month, int day,
                          int hour, int minute, int second, int dst);

#ifdef _WIN32
/* Win32 wrapper for FUN_005045eb: GetLocalTime + GetTimeZoneInformation
 * → derive dst flag → rng_compute_seed → rng_seed.
 *
 * The engine's FUN_005045eb also caches the last UTC year/month/day/hour/
 * minute and skips the GetTimeZoneInformation call when unchanged; we
 * skip that optimisation here — it's called once at boot, so it doesn't
 * matter. */
void rng_seed_from_now(void);
#endif

#endif /* OPENRECET_RNG_H */
