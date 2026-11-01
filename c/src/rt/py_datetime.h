/* datetime, timedelta and calendar, as the engine uses them.
 *
 * py_datetime is a naive datetime: microseconds since 0001-01-01 00:00:00, proleptic
 * Gregorian, years 1..9999 (0 .. 315537897599999999), like Python's datetime.
 * py_timedelta is a signed microsecond count. Python allows +-999999999 days, which does not
 * fit in int64 microseconds; every timedelta the engine builds is added to a datetime at
 * once, and anything beyond PY_TIMEDELTA_MAX_DAYS would overflow that datetime anyway, so
 * constructors report OverflowError there (the same exception class Python raises one step
 * later). This shortcut is the only intended difference and is invisible in results.
 */
#ifndef HC_PY_DATETIME_H
#define HC_PY_DATETIME_H

#include "rt/rt.h"
#include "rt/py_num.h"

typedef struct py_datetime {
    int64_t us;   /* microseconds since 0001-01-01T00:00:00 */
} py_datetime;

typedef struct py_timedelta {
    int64_t us;
} py_timedelta;

#define PY_DATETIME_MAX_US INT64_C(315537897599999999)   /* 9999-12-31 23:59:59.999999 */
#define PY_TIMEDELTA_MAX_DAYS INT64_C(100000000)

/* True when us is a datetime Python can hold. Every py_datetime the engine builds is in range
 * (constructors and arithmetic raise otherwise); data from outside (a restored state, a host
 * clock) is checked with this before it is used, so the add/sub below cannot overflow int64. */
static inline bool py_datetime_in_range(int64_t us)
{
    return us >= 0 && us <= PY_DATETIME_MAX_US;
}

/* datetime(year, month, day, hour, minute, second, microsecond): ValueError for any field out
 * of range (year 1..9999, month 1..12, day 1..monthrange, hour 0..23, minute 0..59,
 * second 0..59, microsecond 0..999999). Fields are int64 so huge parsed values just fail; a
 * parsed digit run that does not even fit int64 is the caller's OverflowError (find_dates
 * catches both), never a wrapped or truncated value. As in CPython, which reads every field
 * as a C int before checking any range, a field outside -2**31 .. 2**31-1 is OverflowError,
 * even when another field is out of range too. */
RT_MUST_CHECK py_exc py_datetime_new(int64_t year, int64_t month, int64_t day, int64_t hour,
                                     int64_t minute, int64_t second, int64_t microsecond,
                                     py_datetime *out);

/* .year .month .day .hour .minute .second .microsecond */
void py_datetime_fields(py_datetime dt, int *year, int *month, int *day, int *hour, int *minute,
                        int *second, int *microsecond);
int py_datetime_year(py_datetime dt);
int py_datetime_month(py_datetime dt);
int py_datetime_day(py_datetime dt);
int py_datetime_hour(py_datetime dt);

/* dt + td and dt - td: OverflowError ("date value out of range") outside 1..9999. */
RT_MUST_CHECK py_exc py_datetime_add(py_datetime dt, py_timedelta td, py_datetime *out);
RT_MUST_CHECK py_exc py_datetime_sub_delta(py_datetime dt, py_timedelta td, py_datetime *out);
/* a - b (always representable) */
py_timedelta py_datetime_sub(py_datetime a, py_datetime b);
int py_datetime_cmp(py_datetime a, py_datetime b);

/* datetime.combine(day.date(), clock.time()) */
py_datetime py_datetime_combine(py_datetime day, py_datetime clock);
/* dt.replace(hour=hour), hour already validated 0..23 */
py_datetime py_datetime_replace_hour(py_datetime dt, int hour);
/* datetime(dt.year, dt.month, dt.day) */
py_datetime py_datetime_date_only(py_datetime dt);

/* timedelta(days=d) for a Python int or float, CPython delta_new/accum exactly:
 *   int: exact.
 *   float: frac, ip = modf(d); us = int(ip) * 86400000000; if frac: f2, ip2 =
 *   modf(86400000000.0 * frac); us += int(ip2); leftover = f2; if leftover: w = round half
 *   away from zero(leftover); if fabs(w - leftover) == 0.5: odd = us & 1;
 *   w = 2.0 * round_half_away((leftover + odd) * 0.5) - odd; us += w.
 * (A naive round(d * 86400e6) differs in about 14% of random cases.) inf: OverflowError;
 * nan: ValueError; abs(d) > PY_TIMEDELTA_MAX_DAYS: OverflowError; complex: TypeError. */
RT_MUST_CHECK py_exc py_timedelta_from_days(py_num days, py_timedelta *out);
RT_MUST_CHECK py_exc py_timedelta_from_hours(int64_t hours, py_timedelta *out);
RT_MUST_CHECK py_exc py_timedelta_from_minutes(int64_t minutes, py_timedelta *out);
/* timedelta(seconds=s) for an int64 count (used by the PAL clock). */
RT_MUST_CHECK py_exc py_timedelta_from_seconds(int64_t seconds, py_timedelta *out);

/* td.total_seconds(): us / 10**6 as a correctly rounded division (differs from
 * (double)us / 1e6 once abs(us) > 2**53). */
double py_timedelta_total_seconds(py_timedelta td);

/* calendar.monthrange(y, m)[1] */
int py_calendar_days_in_month(int year, int month);
bool py_calendar_isleap(int year);

/* The UNIX epoch (1970-01-01T00:00:00) as a py_datetime, for converting PAL clock readings. */
#define PY_DATETIME_UNIX_EPOCH_US INT64_C(62135596800000000)

#endif
