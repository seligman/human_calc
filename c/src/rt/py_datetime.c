/* datetime, timedelta and calendar (rt/py_datetime.h). Integer arithmetic, plus the few
 * double steps of timedelta(days=x) and total_seconds(), done with exact helpers instead of
 * libm (modf and round of doubles below 2**62, scaling by a power of two), so this file needs
 * no -fno-builtin. */
#include "rt/py_datetime.h"

#define US_PER_SECOND INT64_C(1000000)
#define US_PER_MINUTE INT64_C(60000000)
#define US_PER_HOUR INT64_C(3600000000)
#define US_PER_DAY INT64_C(86400000000)

/* CPython reads every datetime() field as a C int (32 bits on all targets) before checking
 * any range: a field outside it is OverflowError even when another field is invalid. */
#define FIELD_MIN (-INT64_C(2147483647) - 1)
#define FIELD_MAX INT64_C(2147483647)

/* Every py_datetime the engine holds is in range (rt/py_datetime.h); anything else would be
 * a broken invariant, which still gets defined results (the nearest end of the range). */
static int64_t in_range(py_datetime dt)
{
    RT_DEBUG_ASSERT(py_datetime_in_range(dt.us));
    if (dt.us < 0) {
        return 0;
    }
    if (dt.us > PY_DATETIME_MAX_US) {
        return PY_DATETIME_MAX_US;
    }
    return dt.us;
}

static bool is_leap(int64_t year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int month_days(int64_t year, int64_t month)
{
    static const int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap(year)) {
        return 29;
    }
    return DAYS[month - 1];
}

/* Days from 0001-01-01 to y-m-d (proleptic Gregorian, y >= 1), counted in 400-year eras of
 * years that start in March, so the leap day is last. */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    int64_t era;
    int64_t yoe;
    int64_t doy;
    int64_t doe;

    y -= m <= 2 ? 1 : 0;
    era = y / 400;
    yoe = y - era * 400;
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 306;
}

/* The inverse of days_from_civil for 0 <= days <= 3652058. */
static void civil_from_days(int64_t days, int *y, int *m, int *d)
{
    int64_t z = days + 306;
    int64_t era = z / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t month = mp < 10 ? mp + 3 : mp - 9;

    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)month;
    *y = (int)(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

/* C only */
py_exc py_datetime_new(int64_t year, int64_t month, int64_t day, int64_t hour, int64_t minute,
                       int64_t second, int64_t microsecond, py_datetime *out)
{
    const int64_t fields[7] = {year, month, day, hour, minute, second, microsecond};
    size_t i;

    for (i = 0; i < 7; i++) {
        if (fields[i] < FIELD_MIN || fields[i] > FIELD_MAX) {
            return PYX_OVERFLOW_ERROR;
        }
    }
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > month_days(year, month)) {
        return PYX_VALUE_ERROR;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59 ||
        microsecond < 0 || microsecond > 999999) {
        return PYX_VALUE_ERROR;
    }
    out->us = days_from_civil(year, month, day) * US_PER_DAY + hour * US_PER_HOUR +
              minute * US_PER_MINUTE + second * US_PER_SECOND + microsecond;
    return PYX_OK;
}

/* C only */
void py_datetime_fields(py_datetime dt, int *year, int *month, int *day, int *hour, int *minute,
                        int *second, int *microsecond)
{
    int64_t us = in_range(dt);
    int64_t rem = us % US_PER_DAY;
    int y;
    int m;
    int d;

    civil_from_days(us / US_PER_DAY, &y, &m, &d);
    if (year != NULL) {
        *year = y;
    }
    if (month != NULL) {
        *month = m;
    }
    if (day != NULL) {
        *day = d;
    }
    if (hour != NULL) {
        *hour = (int)(rem / US_PER_HOUR);
    }
    if (minute != NULL) {
        *minute = (int)(rem % US_PER_HOUR / US_PER_MINUTE);
    }
    if (second != NULL) {
        *second = (int)(rem % US_PER_MINUTE / US_PER_SECOND);
    }
    if (microsecond != NULL) {
        *microsecond = (int)(rem % US_PER_SECOND);
    }
}

/* C only */
int py_datetime_year(py_datetime dt)
{
    int year;

    py_datetime_fields(dt, &year, NULL, NULL, NULL, NULL, NULL, NULL);
    return year;
}

/* C only */
int py_datetime_month(py_datetime dt)
{
    int month;

    py_datetime_fields(dt, NULL, &month, NULL, NULL, NULL, NULL, NULL);
    return month;
}

/* C only */
int py_datetime_day(py_datetime dt)
{
    int day;

    py_datetime_fields(dt, NULL, NULL, &day, NULL, NULL, NULL, NULL);
    return day;
}

/* C only */
int py_datetime_hour(py_datetime dt)
{
    return (int)(in_range(dt) % US_PER_DAY / US_PER_HOUR);
}

/* C only */
py_exc py_datetime_add(py_datetime dt, py_timedelta td, py_datetime *out)
{
    int64_t us;

    if (!rt_i64_add(dt.us, td.us, &us) || !py_datetime_in_range(us)) {
        return PYX_OVERFLOW_ERROR;
    }
    out->us = us;
    return PYX_OK;
}

/* C only */
py_exc py_datetime_sub_delta(py_datetime dt, py_timedelta td, py_datetime *out)
{
    int64_t us;

    if (!rt_i64_sub(dt.us, td.us, &us) || !py_datetime_in_range(us)) {
        return PYX_OVERFLOW_ERROR;
    }
    out->us = us;
    return PYX_OK;
}

/* C only */
py_timedelta py_datetime_sub(py_datetime a, py_datetime b)
{
    py_timedelta td;

    /* Exact for datetimes in range; saturates for values that never occur. */
    if (!rt_i64_sub(a.us, b.us, &td.us)) {
        td.us = a.us < b.us ? INT64_MIN : INT64_MAX;
    }
    return td;
}

/* C only */
int py_datetime_cmp(py_datetime a, py_datetime b)
{
    return (a.us > b.us) - (a.us < b.us);
}

/* C only */
py_datetime py_datetime_combine(py_datetime day, py_datetime clock)
{
    py_datetime r;
    int64_t d = in_range(day);

    r.us = d - d % US_PER_DAY + in_range(clock) % US_PER_DAY;
    return r;
}

/* C only */
py_datetime py_datetime_replace_hour(py_datetime dt, int hour)
{
    py_datetime r;
    int64_t us = in_range(dt);

    RT_DEBUG_ASSERT(hour >= 0 && hour <= 23);
    if (hour < 0 || hour > 23) {
        hour = hour < 0 ? 0 : 23;
    }
    r.us = us - us % US_PER_DAY + (int64_t)hour * US_PER_HOUR + us % US_PER_HOUR;
    return r;
}

/* C only */
py_datetime py_datetime_date_only(py_datetime dt)
{
    py_datetime r;
    int64_t us = in_range(dt);

    r.us = us - us % US_PER_DAY;
    return r;
}

/* The integral part of x toward zero, as a double (modf's ip; x - trunc_small(x) is its frac,
 * exact). Needs abs(x) < 2**62, which every caller has checked. */
static double trunc_small(double x)
{
    return (double)(int64_t)x;
}

/* C round(): half away from zero, for abs(x) < 2**62. */
static double round_half_away(double x)
{
    double t = trunc_small(x);
    double f = x - t;

    if (f >= 0.5) {
        t += 1.0;
    } else if (f <= -0.5) {
        t -= 1.0;
    }
    return t;
}

/* timedelta(days=d) for a float: CPython's delta_new/accum (see the header). */
static py_exc timedelta_from_float_days(double d, py_timedelta *out)
{
    const double max_days = (double)PY_TIMEDELTA_MAX_DAYS;
    double ip;
    double frac;
    double leftover = 0.0;
    int64_t us;

    if (d != d) {
        return PYX_VALUE_ERROR;
    }
    if (!(d >= -max_days && d <= max_days)) {
        return PYX_OVERFLOW_ERROR;
    }
    ip = trunc_small(d);
    frac = d - ip;
    us = (int64_t)ip * US_PER_DAY;
    if (frac != 0.0) {
        double prod = 86400000000.0 * frac;
        double ip2 = trunc_small(prod);

        us += (int64_t)ip2;
        leftover += prod - ip2;
    }
    if (leftover != 0.0) {
        double whole = round_half_away(leftover);
        double diff = whole - leftover;

        if (diff == 0.5 || diff == -0.5) {
            /* An exact tie: round half to even on the total. */
            double odd = us % 2 != 0 ? 1.0 : 0.0;
            whole = 2.0 * round_half_away((leftover + odd) * 0.5) - odd;
        }
        us += (int64_t)whole;
    }
    out->us = us;
    return PYX_OK;
}

/* C only */
py_exc py_timedelta_from_days(py_num days, py_timedelta *out)
{
    switch (days.kind) {
    case PYN_INT:
        if (days.big != NULL || days.small > PY_TIMEDELTA_MAX_DAYS ||
            days.small < -PY_TIMEDELTA_MAX_DAYS) {
            return PYX_OVERFLOW_ERROR;
        }
        out->us = days.small * US_PER_DAY;
        return PYX_OK;
    case PYN_FLOAT:
        return timedelta_from_float_days(days.f, out);
    case PYN_COMPLEX:
    default:
        return PYX_TYPE_ERROR;
    }
}

static py_exc timedelta_from_count(int64_t count, int64_t unit, int64_t per_day,
                                   py_timedelta *out)
{
    int64_t limit = PY_TIMEDELTA_MAX_DAYS * per_day;

    if (count > limit || count < -limit) {
        return PYX_OVERFLOW_ERROR;
    }
    out->us = count * unit;
    return PYX_OK;
}

/* C only */
py_exc py_timedelta_from_hours(int64_t hours, py_timedelta *out)
{
    return timedelta_from_count(hours, US_PER_HOUR, 24, out);
}

/* C only */
py_exc py_timedelta_from_minutes(int64_t minutes, py_timedelta *out)
{
    return timedelta_from_count(minutes, US_PER_MINUTE, 24 * 60, out);
}

/* C only */
py_exc py_timedelta_from_seconds(int64_t seconds, py_timedelta *out)
{
    return timedelta_from_count(seconds, US_PER_SECOND, 24 * 60 * 60, out);
}

/* 2**e as a double, for -1022 <= e <= 1023 (exact; no libm). */
static double pow2(int e)
{
    uint64_t bits = (uint64_t)(e + 1023) << 52;
    double r;

    rt_memcpy(&r, &bits, sizeof r);
    return r;
}

/* C only */
double py_timedelta_total_seconds(py_timedelta td)
{
    const uint64_t million = 1000000;
    const uint64_t two53 = UINT64_C(1) << 53;
    uint64_t a = rt_i64_magnitude(td.us);
    uint64_t q;
    uint64_t r;
    uint64_t m;
    uint64_t low;
    int bits = 0;
    int frac_bits;
    int i;
    bool sticky;
    double v;

    if (a <= two53) {
        /* Both operands are exact doubles: one IEEE division is correctly rounded. */
        return (double)td.us / 1e6;
    }
    /* us / 10**6 rounded once, half to even: the integer quotient (34 to 44 bits here) and
     * the remainder's binary fraction make a 55-bit significand (53 bits, a guard bit and a
     * round bit) plus a sticky bit for the rest. */
    q = a / million;
    r = a % million;
    while ((q >> bits) != 0) {
        bits++;
    }
    frac_bits = 55 - bits;
    m = q;
    for (i = 0; i < frac_bits; i++) {
        r <<= 1;
        m <<= 1;
        if (r >= million) {
            r -= million;
            m |= 1;
        }
    }
    sticky = r != 0;
    low = m & 3;
    m >>= 2;
    if ((low & 2) != 0 && ((low & 1) != 0 || sticky || (m & 1) != 0)) {
        m += 1;
    }
    /* m <= 2**53, an exact double; scaling by a power of two is exact (normal range). */
    v = (double)m * pow2(2 - frac_bits);
    return td.us < 0 ? -v : v;
}

/* C only */
int py_calendar_days_in_month(int year, int month)
{
    return month_days(year, month);
}

/* C only */
bool py_calendar_isleap(int year)
{
    return is_leap(year);
}
