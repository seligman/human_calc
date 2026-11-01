/* Platform layer (rt_pal.h): clock, local time zone offset, floating point environment. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "rt/rt_pal.h"

#include <time.h>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)) && !defined(_M_ARM64EC)
#include <float.h>
#define RT_PAL_MSVC_X86 1
#else
#define RT_PAL_MSVC_X86 0
#endif

#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#elif defined(_MSC_VER)
#pragma fenv_access(on)
#endif

/* C only */
int rt_pal_utc_now_us(int64_t *out)
{
    struct timespec ts;
    int64_t us;
    int64_t ns;

#if defined(_WIN32)
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return -1;
    }
#else
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }
#endif
    ns = (int64_t)ts.tv_nsec;
    if (ns < 0 || ns >= 1000000000) {
        return -1;
    }
    if (!rt_i64_mul((int64_t)ts.tv_sec, 1000000, &us) || !rt_i64_add(us, ns / 1000, &us)) {
        return -1;
    }
    *out = us;
    return 0;
}

/* Days since 1970-01-01 of a proleptic Gregorian date. */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    int64_t era;
    int64_t yoe;
    int64_t doy;
    int64_t doe;

    y -= m <= 2 ? 1 : 0;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* C only */
int rt_pal_local_offset_seconds(int64_t utc_seconds, int32_t *out)
{
    time_t t;
    struct tm tm;
    int64_t local;
    int64_t offset;
    int64_t sec;

    /* Years 1..9999 are about +-3e11 seconds; anything beyond is not a time we ask about. */
    if (utc_seconds < -100000000000 || utc_seconds > 300000000000) {
        return -1;
    }
    if (sizeof(time_t) < sizeof(int64_t) &&
        (utc_seconds < -2147483647 - 1 || utc_seconds > 2147483647)) {
        return -1;
    }
    t = (time_t)utc_seconds;
#if defined(_WIN32)
    if (localtime_s(&tm, &t) != 0) {
        return -1;
    }
#else
    if (localtime_r(&t, &tm) == NULL) {
        return -1;
    }
#endif
    /* A leap second reads as :59, as Python's datetime.fromtimestamp clamps it. */
    sec = tm.tm_sec > 59 ? 59 : tm.tm_sec;
    local = days_from_civil((int64_t)tm.tm_year + 1900, (int64_t)tm.tm_mon + 1, tm.tm_mday) * 86400 +
            (int64_t)tm.tm_hour * 3600 + (int64_t)tm.tm_min * 60 + sec;
    offset = local - utc_seconds;
    if (offset < -2 * 86400 || offset > 2 * 86400) {
        return -1;
    }
    *out = (int32_t)offset;
    return 0;
}

/* C only */
void rt_pal_fp_enter(rt_pal_fp_state *saved)
{
    (void)fegetenv(&saved->env);
    (void)fesetenv(FE_DFL_ENV);
#if RT_PAL_MSVC_X86
    {
        unsigned int current = 0;
        (void)_controlfp_s(&saved->control, 0, 0);
        (void)_controlfp_s(&current, _DN_SAVE, _MCW_DN);
    }
#else
    saved->control = 0;
#endif
}

/* C only */
void rt_pal_fp_leave(const rt_pal_fp_state *saved)
{
    (void)fesetenv(&saved->env);
#if RT_PAL_MSVC_X86
    {
        unsigned int current = 0;
        (void)_controlfp_s(&current, saved->control & _MCW_DN, _MCW_DN);
    }
#endif
}
