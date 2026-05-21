#include "rng.h"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif

uint32_t g_rng_seed = 1;

void rng_seed(uint32_t seed)
{
    g_rng_seed = seed;
}

uint16_t rng_next15(void)
{
    g_rng_seed = g_rng_seed * 0x343fdu + 0x269ec3u;
    return (uint16_t)((g_rng_seed >> 16) & 0x7fffu);
}

float rng_next_unit(void)
{
    return (float)rng_next15() / 32768.0f;
}

int32_t rng_compute_seed(int year, int month, int day,
                         int hour, int minute, int second, int dst)
{
    /* Day-of-year offsets per month, from the table at 0x006039b4 in the
     * unpacked binary (read via tools/analyze/pe.py). Slot 0 is dead text
     * (engine literal 365 — never indexed for a real month value); slot 1
     * is -1 so January day=1 maps to day-of-year 0. */
    static const int32_t doy[13] = {
         365,        /* month 0 (invalid; engine table holds 0x16d) */
         -1,         /* January  — base   0  */
         30,         /* February — base 31  */
         58,         /* March    — base 59  (non-leap year)  */
         89,
        119,
        150,
        180,
        211,
        242,
        272,
        303,
        333,
    };
    if (month < 1 || month > 12) return -1;
    int yr_1900 = year - 1900;
    if (yr_1900 < 0x46 || yr_1900 > 0x8a) return -1;

    int32_t adj_day = doy[month] + day;
    if ((yr_1900 & 3) == 0 && month > 2) adj_day += 1;

    /* Engine constants from .data at 0x006038d0:
     *   timezone offset:  28800s  = 8 h = PST (default MSVC _timezone)
     *   epoch literal:    0x7c558180  (synthetic shift, not a real epoch)
     *   DST bias:         -3600s (default MSVC _dstbias)
     *
     * The engine's literal addition pattern produces a wrap-around uint32
     * for any year past ~2000 — that's fine, it's just feeding an LCG. */
    static const int32_t TZ_OFFSET_SECONDS = 0x7080;       /* 28800 */
    static const int32_t EPOCH_CONSTANT    = 0x7c558180;
    static const int32_t DST_BIAS_SECONDS  = -3600;

    int32_t days = yr_1900 * 365 + adj_day + (year - 1901) / 4;
    int32_t s = (((hour + days * 24) * 60 + minute) * 60
                 + TZ_OFFSET_SECONDS + EPOCH_CONSTANT + second);
    if (dst == 1) s += DST_BIAS_SECONDS;
    return s;
}

#ifdef _WIN32
void rng_seed_from_now(void)
{
    SYSTEMTIME local = {0};
    GetLocalTime(&local);

    TIME_ZONE_INFORMATION tz = {0};
    DWORD r = GetTimeZoneInformation(&tz);
    int dst;
    if (r == TIME_ZONE_ID_INVALID) {
        dst = -1;
    } else if (r == TIME_ZONE_ID_DAYLIGHT
               && tz.DaylightDate.wMonth != 0
               && tz.DaylightBias != 0) {
        dst = 1;
    } else {
        dst = 0;
    }

    int32_t s = rng_compute_seed(local.wYear, local.wMonth, local.wDay,
                                 local.wHour, local.wMinute, local.wSecond,
                                 dst);
    rng_seed((uint32_t)s);
}
#endif
