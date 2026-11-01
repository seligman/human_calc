/* Platform abstraction layer: the only OS services the library uses.
 *
 * The engine needs a real clock only when no date override is set (the CLI case): Python's
 * get_now() reads datetime.now() (local wall time) and datetime.now(UTC) on every call. A
 * host can replace both with hc_set_clock (include/human_calc.h); these are the portable
 * defaults used otherwise.
 *
 * Implementation notes (rt_pal.c), chosen at compile time:
 *   _WIN32 (MSVC and MinGW alike, not _MSC_VER): timespec_get(TIME_UTC) (UCRT) and
 *             localtime_s(&tm, &t) (Microsoft argument order), as CPython uses there.
 *   __APPLE__: clock_gettime(CLOCK_REALTIME) (macOS 10.12, iOS 10; timespec_get would need
 *             macOS 10.15 / iOS 13 and is a hard error below that deployment target) and
 *             localtime_r. No feature macros (they would hide parts of Apple's headers).
 *   other POSIX (glibc, musl, BSD): rt_pal.c starts with #define _POSIX_C_SOURCE 200809L
 *             (strict -std=c11 hides localtime_r and clock_gettime otherwise), then
 *             clock_gettime(CLOCK_REALTIME) and localtime_r.
 *   offset:   the broken-down local time minus UTC, computed with a days-from-civil formula
 *             (no timegm, which is not portable). The OS time zone rules apply, including the
 *             TZ environment variable, as for CPython; the library itself reads no variable.
 * No locale, no global state.
 *
 * Floating point environment: results depend on the calling thread's rounding mode,
 * flush-to-zero / denormals-are-zero and trap settings, which the host owns (audio and
 * graphics code often enables flush-to-zero; 3e-323 - 2.5e-323 then changes). Every public
 * entry point that computes (hc_calc, hc_restore, hc_set_currency_json, hc_set_tz_offset)
 * brackets its work with rt_pal_fp_enter / rt_pal_fp_leave: save the environment, install the
 * default one (round to nearest, no traps, no flush-to-zero: fesetenv(FE_DFL_ENV), plus
 * _controlfp_s for the denormal control on MSVC x86/x64), restore the host's on return. The
 * host's callbacks run inside that default environment.
 */
#ifndef HC_RT_PAL_H
#define HC_RT_PAL_H

#include "rt/rt.h"

#include <fenv.h>

/* Microseconds since 1970-01-01T00:00:00Z. 0 on success, -1 if the clock is unavailable. */
int rt_pal_utc_now_us(int64_t *out);

/* Local time minus UTC, in seconds, at the given UTC instant (seconds since the UNIX epoch).
 * 0 on success, -1 on failure (the caller then uses 0). */
int rt_pal_local_offset_seconds(int64_t utc_seconds, int32_t *out);

/* The saved floating point environment of the calling thread. */
typedef struct rt_pal_fp_state {
    fenv_t env;
    unsigned int control;   /* MSVC x86/x64: the _controlfp_s word; unused elsewhere */
} rt_pal_fp_state;

void rt_pal_fp_enter(rt_pal_fp_state *saved);
void rt_pal_fp_leave(const rt_pal_fp_state *saved);

#endif
