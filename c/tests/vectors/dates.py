"""Vectors for tests/rt/test_dates.c: datetime, timedelta and calendar semantics of
src/rt/py_datetime.c (DESIGN.md 5.6, 13.3), plus the month stepping of
date_value.py DateValue._add_months_helepr.

A datetime is written as its microseconds since 0001-01-01T00:00:00 (py_datetime.us), a
timedelta as its microseconds. A result column is that int or {"exc": "<Class>"}.

Rows (columns after the case name):
    new      y, mo, d, h, mi, s, us, datetime(y, mo, d, h, mi, s, us)  (int64 fields)
    fields   dt, [year, month, day, hour, minute, second, microsecond]
    add      dt, td, dt + timedelta(microseconds=td)
    subd     dt, td, dt - timedelta(microseconds=td)
    sub      a, b, a - b
    cmp      a, b, (a > b) - (a < b)
    combine  day, clock, datetime.combine(day.date(), clock.time())
    hour     dt, h, dt.replace(hour=h)
    dateonly dt, datetime(dt.year, dt.month, dt.day)
    days     x, dev, timedelta(days=x)   x an int or a float
    hours    n, dev, timedelta(hours=n)
    minutes  n, dev, timedelta(minutes=n)
    seconds  n, dev, timedelta(seconds=n)
    daysadd  dt, x, sign, dt + timedelta(days=x) (sign 1) or dt - timedelta(days=x) (sign -1),
             whichever step raises
    tsec     td, timedelta(microseconds=td).total_seconds()
    mrange   y, m, calendar.monthrange(y, m)[1]
    isleap   y, calendar.isleap(y)
    mstep    dt, count, DateValue._add_months_helepr(dt, count) (the first of the month)

dev is true when the expected column is not what Python returns but the documented C
shortcut (DESIGN.md 15.3, rt/py_datetime.h): a timedelta beyond PY_TIMEDELTA_MAX_DAYS days is
OverflowError when built. The script checks for each such row that Python's timedelta, added
to or subtracted from any datetime, overflows, so the engine's result is the same; daysadd
rows test that composite directly, against Python, with no exception.
"""

import vecutil

import calendar
import math
from datetime import datetime, timedelta

EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)
MAX_US = (datetime.max - EPOCH0) // US
MAX_DAYS = 100_000_000  # PY_TIMEDELTA_MAX_DAYS
I64_MIN = vecutil.INT64_MIN
I64_MAX = vecutil.INT64_MAX
US_PER_DAY = 86_400_000_000

assert MAX_US == 315537897599999999


def dt_us(dt):
    return (dt - EPOCH0) // US


def us_dt(us):
    return EPOCH0 + timedelta(microseconds=us)


def td_us(td):
    return td // US


def res(fn, *args):
    r = vecutil.result(fn, *args)
    if isinstance(r, datetime):
        return dt_us(r)
    if isinstance(r, timedelta):
        return td_us(r)
    return r


def overflows_everywhere(td):
    """Every datetime plus or minus td is out of range."""
    return abs(td_us(td)) > MAX_US


def write_delta(w, case, make, arg, limit):
    """A timedelta constructor row; C refuses abs(arg) > limit (arg in the call's own unit)."""
    r = vecutil.result(make, arg)
    dev = False
    if isinstance(r, timedelta):
        if abs(arg) > limit:
            assert overflows_everywhere(r), (case, arg)
            dev = True
            r = vecutil.Exc("OverflowError")
        else:
            r = td_us(r)
            assert I64_MIN <= r <= I64_MAX, (case, arg)
    w.row(case, arg, dev, r)


def rand_dt(rng):
    r = rng.random()
    if r < 0.6:
        return rng.randint(0, MAX_US)
    if r < 0.7:
        return rng.randint(0, 10 ** 12)
    if r < 0.8:
        return MAX_US - rng.randint(0, 10 ** 12)
    if r < 0.9:
        return rng.randint(0, 3652058) * US_PER_DAY + rng.choice([0, 1, US_PER_DAY - 1])
    # A time of day (0001-01-01 plus a few days), as the engine keeps them.
    return rng.randint(0, 5 * US_PER_DAY)


def rand_td(rng):
    r = rng.random()
    if r < 0.3:
        return rng.randint(-MAX_US - 10, MAX_US + 10)
    if r < 0.5:
        return rng.randint(-10 ** 12, 10 ** 12)
    if r < 0.6:
        return rng.randint(I64_MIN, I64_MAX)
    if r < 0.8:
        return rng.randint(-3_700_000, 3_700_000) * US_PER_DAY
    return rng.randint(-10 ** 6, 10 ** 6)


EDGE_FIELDS = [0, 1, -1, 2, 9, 10, 11, 12, 13, 23, 24, 28, 29, 30, 31, 32, 59, 60, 99, 100,
               999999, 1000000, 9999, 10000, 2 ** 31 - 1, 2 ** 31, -(2 ** 31), -(2 ** 31) - 1,
               I64_MAX, I64_MIN]
LEAP_YEARS = [1, 4, 100, 200, 300, 400, 1600, 1700, 1800, 1900, 2000, 2004, 2020, 2021, 2100,
              2400, 9996, 9999]


def write_new(w, *f):
    w.row("new", *f, res(datetime, *f))


def frac_ties(rng, n):
    """Floats whose day fraction times 86400000000 ends in exactly .5 (k / 2**14, k odd),
    with integer parts of every size."""
    out = []
    for _ in range(n):
        k = rng.randrange(1, 1 << 14, 2)
        ip = rng.choice([0, 0, 1, 2, 3, rng.randint(0, 1000), rng.randint(0, 3_000_000),
                         rng.randint(0, 2 ** 37)])
        x = ip + k / 16384.0
        if rng.random() < 0.5:
            x = -x
        out.append(x)
    return out


def near_ties(rng, n):
    """Floats whose leftover microsecond fraction is close to (or at) one half."""
    out = []
    for _ in range(n):
        ip = rng.choice([0, 1, rng.randint(0, 4_000_000), rng.randint(0, MAX_DAYS)])
        m = rng.randint(0, US_PER_DAY - 1)
        x = ip + (m + 0.5) / US_PER_DAY
        for _ in range(rng.randint(0, 3)):
            x = math.nextafter(x, math.inf if rng.random() < 0.5 else -math.inf)
        out.append(-x if rng.random() < 0.5 else x)
    return out


def rand_days(rng):
    r = rng.random()
    if r < 0.25:
        x = 10.0 ** rng.uniform(-16, 8.05)
    elif r < 0.4:
        x = rng.uniform(0, 4_000_000)
    elif r < 0.5:
        x = rng.randint(0, 4_000_000) + 10.0 ** rng.uniform(-15, 0)
    elif r < 0.6:
        x = vecutil.rand_double(rng)
    elif r < 0.7:
        x = rng.randint(1, 10 ** 6) / rng.choice([3, 7, 24, 1440, 86400, 864000, 8.64e10, 12])
    elif r < 0.8:
        x = rng.uniform(0, 2 * MAX_DAYS)
    elif r < 0.9:
        x = rng.randint(-1000, 1000) * rng.choice([1 / 86400, 1 / 1440, 1 / 24, 365 / 12, 28.0,
                                                   365.0, 1e-6 / 86400, 1e-7 / 86400])
    else:
        x = float(rng.randint(-4_000_000, 4_000_000))
    return -x if rng.random() < 0.5 else x


DAY_FLOATS = [0.0, -0.0, 0.5, -0.5, 1.0, -1.0, 1 / 3, -1 / 3, 2 / 3, 1 / 86400, (1 / 3) / 86400,
              2.5e-6 / 86400, 5e-7 / 86400, 1e-7 / 86400, 0.1 / 86400, 1.1 / 24, 365 / 12,
              (365 / 12) * 0.5, 28.0 * 0.25, 2914000.0, 3000000.0, 737700.0, 3652058.0,
              3652059.0, 3652058.999999999, 1e8, -1e8, math.nextafter(1e8, 0),
              math.nextafter(1e8, math.inf), -math.nextafter(1e8, math.inf), 999999999.0,
              999999999.9999999, 1e9, -1e9, 1e300, -1e300, float("inf"), float("-inf"),
              float("nan"), 5e-324, -5e-324, 2.0 ** -14, 3 * 2.0 ** -14, -(2.0 ** -14),
              2.0 ** -15, 2.0 ** -40, 1e-300, 4503599627370495.5, 2.0 ** 52, 2.0 ** 62,
              2.0 ** 63, -(2.0 ** 63), 1e-12, 123456.789, -123456.789]
DAY_INTS = [0, 1, -1, 2, 3652058, 3652059, -3652059, MAX_DAYS, -MAX_DAYS, MAX_DAYS + 1,
            -MAX_DAYS - 1, 999999999, -999999999, 10 ** 9, -(10 ** 9), I64_MAX, I64_MIN,
            I64_MAX + 1, I64_MIN - 1, 10 ** 30, -(10 ** 30)]


def tsec_values(rng, n):
    two53 = 1 << 53
    out = [0, 1, -1, 999999, 1000000, -1000000, two53 - 1, two53, two53 + 1, -two53,
           -two53 - 1, two53 + 2, two53 + 3, I64_MAX, I64_MIN, I64_MIN + 1, MAX_US, -MAX_US,
           MAX_US + 1, US_PER_DAY * 3652059 - 1, 15625 * (1 << 49), 15625 * ((1 << 49) + 1),
           10 ** 18, 10 ** 18 + 1]
    for _ in range(n):
        r = rng.random()
        if r < 0.3:
            v = rng.randint(I64_MIN, I64_MAX)
        elif r < 0.5:
            v = rng.randint(-MAX_US, MAX_US)
        elif r < 0.6:
            v = rng.randint(-two53, two53)
        elif r < 0.7:
            v = rng.randint(two53 - 10 ** 6, two53 + 10 ** 6)
        elif r < 0.8:
            v = 15625 * rng.randint(0, I64_MAX // 15625)  # dyadic quotients: exact
        else:
            # Next to the midpoint of two doubles: the rounding direction rests on the
            # last few bits of the remainder.
            q = float(rng.randint(two53 // 10 ** 6 + 1, I64_MAX // 10 ** 6 - 1)) + rng.random()
            lo = q
            hi = math.nextafter(q, math.inf)
            mid2 = (int(lo * 2 ** 20) + int(hi * 2 ** 20))  # 2 * midpoint * 2**20 (exact)
            v = (mid2 * 10 ** 6) // (2 ** 21) + rng.randint(-2, 2)
        out.append(v if rng.random() < 0.7 or v == I64_MIN else -v)
    return out


def months_reference(us, count):
    """date_value.py _add_months_helepr, on datetime: one month per step, raising
    ValueError when the year leaves 1..9999."""
    value = us_dt(us)
    value = datetime(value.year, value.month, 1)
    while count > 0:
        count -= 1
        if value.month == 12:
            value = datetime(value.year + 1, 1, 1)
        else:
            value = datetime(value.year, value.month + 1, 1)
    while count < 0:
        count += 1
        if value.month == 1:
            value = datetime(value.year - 1, 12, 1)
        else:
            value = datetime(value.year, value.month - 1, 1)
    return value


def engine_months():
    """The engine's own _add_months_helepr, when it still exists (a cross-check only)."""
    try:
        calc = vecutil.import_calc()
        dv = calc.date_value.DateValue(datetime(2000, 1, 1), False)
        return dv._add_months_helepr
    except (ImportError, AttributeError):
        return None


def main():
    with vecutil.Writer("dates") as w:
        rng = w.rng

        # ---- datetime() ----
        for f in EDGE_FIELDS:
            for pos in range(7):
                base = [2021, 6, 15, 12, 30, 45, 123456]
                base[pos] = f
                write_new(w, *base)
        write_new(w, 0, 2 ** 31, 1, 0, 0, 0, 0)
        write_new(w, 0, 1, 1, 0, 0, 0, 2 ** 31)
        write_new(w, 10000, 13, 32, 24, 60, 60, -(2 ** 31) - 1)
        write_new(w, 1, 1, 1, 0, 0, 0, 0)
        write_new(w, 9999, 12, 31, 23, 59, 59, 999999)
        write_new(w, 1970, 1, 1, 0, 0, 0, 0)
        write_new(w, 2000, 1, 1, 0, 0, 0, 0)
        for y in LEAP_YEARS:
            for m in range(1, 13):
                for d in (0, 28, 29, 30, 31, 32):
                    write_new(w, y, m, d, 0, 0, 0, 0)
        for _ in range(w.pick(quick=1500, full=60000)):
            f = [rng.randint(1, 9999), rng.randint(1, 12), rng.randint(1, 31), rng.randint(0, 23),
                 rng.randint(0, 59), rng.randint(0, 59), rng.randint(0, 999999)]
            if rng.random() < 0.15:
                f[rng.randrange(7)] = rng.choice(EDGE_FIELDS + [rng.randint(-50, 50)])
            write_new(w, *f)

        # ---- fields and the small operations ----
        fixed = [0, 1, US_PER_DAY - 1, US_PER_DAY, MAX_US, MAX_US - 1,
                 MAX_US - US_PER_DAY + 1, 62135596800000000, 63082281600000000]
        for y in LEAP_YEARS:
            for md in ((1, 1), (2, 28), (2, 29) if calendar.isleap(y) else (3, 1), (3, 1),
                       (12, 31)):
                fixed.append(dt_us(datetime(y, md[0], md[1])))
        dts = fixed + [rand_dt(rng) for _ in range(w.pick(quick=1500, full=100000))]
        for us in dts:
            dt = us_dt(us)
            w.row("fields", us, [dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
                                 dt.microsecond])
            w.row("dateonly", us, dt_us(datetime(dt.year, dt.month, dt.day)))
            h = rng.randint(0, 23)
            w.row("hour", us, h, dt_us(dt.replace(hour=h)))
            other = rand_dt(rng) if rng.random() < 0.8 else us
            w.row("combine", us, other,
                  dt_us(datetime.combine(dt.date(), us_dt(other).time())))
            w.row("sub", us, other, td_us(dt - us_dt(other)))
            w.row("cmp", us, other, (dt > us_dt(other)) - (dt < us_dt(other)))

        # ---- datetime +- timedelta ----
        pairs = []
        for us in (0, 1, MAX_US, MAX_US - 1, 1000):
            for td in (0, 1, -1, MAX_US, -MAX_US, MAX_US + 1, -MAX_US - 1, I64_MAX, I64_MIN,
                       I64_MIN + 1, US_PER_DAY, -US_PER_DAY):
                pairs.append((us, td))
        for _ in range(w.pick(quick=1500, full=100000)):
            us = rand_dt(rng)
            td = rand_td(rng)
            if rng.random() < 0.15:
                # land right next to an end of the range
                td = rng.choice([0, MAX_US]) - us + rng.randint(-2, 2)
            pairs.append((us, td))
        for us, td in pairs:
            dt = us_dt(us)
            w.row("add", us, td, res(lambda: dt + timedelta(microseconds=td)))
            w.row("subd", us, td, res(lambda: dt - timedelta(microseconds=td)))

        # ---- timedelta constructors ----
        days = (DAY_FLOATS + DAY_INTS + frac_ties(rng, w.pick(quick=300, full=20000))
                + near_ties(rng, w.pick(quick=300, full=20000))
                + [rand_days(rng) for _ in range(w.pick(quick=2500, full=300000))]
                + [rng.randint(-MAX_DAYS - 5, MAX_DAYS + 5) for _ in range(w.pick(quick=50, full=2000))])
        ties = 0
        for x in days:
            if isinstance(x, float) and math.isfinite(x) and abs(x) <= MAX_DAYS:
                f2 = math.modf(86400000000.0 * math.modf(x)[0])[0]
                ties += abs(f2) == 0.5
            write_delta(w, "days", lambda v: timedelta(days=v), x, MAX_DAYS)
        assert ties >= 100, ties

        counts = [0, 1, -1, 5, -5, 24, -24, 2 ** 31 - 1, -(2 ** 31), I64_MAX, I64_MIN]
        for case, per_day in (("hours", 24), ("minutes", 1440), ("seconds", 86400)):
            limit = MAX_DAYS * per_day
            vals = counts + [limit, -limit, limit + 1, -limit - 1, 999999999 * per_day,
                             1000000000 * per_day]
            for _ in range(w.pick(quick=300, full=20000)):
                r = rng.random()
                if r < 0.4:
                    vals.append(rng.randint(-10 ** 6, 10 ** 6))
                elif r < 0.7:
                    vals.append(rng.randint(-limit, limit))
                elif r < 0.9:
                    vals.append(rng.randint(-2 * limit, 2 * limit))
                else:
                    vals.append(rng.randint(I64_MIN, I64_MAX))
            for n in vals:
                if not I64_MIN <= n <= I64_MAX:
                    continue
                write_delta(w, case, lambda v, c=case: timedelta(**{c: v}), n, limit)

        # ---- dt + timedelta(days=x) and dt - timedelta(days=x), against Python ----
        for _ in range(w.pick(quick=2000, full=150000)):
            us = rand_dt(rng)
            r = rng.random()
            if r < 0.6:
                x = rand_days(rng)
            elif r < 0.7:
                x = rng.choice(DAY_FLOATS + DAY_INTS)
            elif r < 0.8:
                x = rng.choice(frac_ties(rng, 1) + near_ties(rng, 1))
            else:
                # near the ends of the range
                x = (rng.choice([0, MAX_US]) - us) / US_PER_DAY + rng.uniform(-1e-5, 1e-5)
            sign = rng.choice([1, -1])
            dt = us_dt(us)
            if sign == 1:
                out = res(lambda: dt + timedelta(days=x))
            else:
                out = res(lambda: dt - timedelta(days=x))
            w.row("daysadd", us, x, sign, out)

        # ---- total_seconds ----
        for v in tsec_values(rng, w.pick(quick=3000, full=300000)):
            w.row("tsec", v, timedelta(microseconds=v).total_seconds())

        # ---- calendar ----
        years = sorted(set(LEAP_YEARS + list(range(1, 10000, w.pick(quick=37, full=1)))
                           + [0, -1, -4, -100, -400, 10000, 10400]))
        for y in years:
            w.row("isleap", y, calendar.isleap(y))
            for m in range(1, 13):
                w.row("mrange", y, m, calendar.monthrange(y, m)[1])

        # ---- month stepping (date_value.py _add_months_helepr) ----
        engine = engine_months()
        steps = []
        for us in (0, MAX_US, dt_us(datetime(2021, 1, 31)), dt_us(datetime(2020, 2, 29, 12))):
            for c in (0, 1, -1, 11, 12, 13, -11, -12, -13, 119987, 119988, -119988, 1200,
                      -1200):
                steps.append((us, c))
        for y, m, c in ((1, 12, -11), (1, 12, -12), (1, 2, -1), (1, 2, -2), (9999, 1, 11),
                        (9999, 1, 12), (9999, 11, 1), (9999, 11, 2), (2, 1, -12), (2, 1, -13)):
            steps.append((dt_us(datetime(y, m, 15, 6)), c))
        for _ in range(w.pick(quick=600, full=20000)):
            us = rand_dt(rng)
            c = rng.randint(-300, 300) if rng.random() < 0.7 else rng.randint(-3000, 3000)
            steps.append((us, c))
        # Long walks (up to 120,000 Python loop steps each) are few.
        for _ in range(w.pick(quick=16, full=150)):
            steps.append((rand_dt(rng), rng.randint(-125000, 125000)))
        for _ in range(w.pick(quick=4, full=20)):
            steps.append((rand_dt(rng), rng.choice([I64_MAX, I64_MIN, 2 ** 40, -(2 ** 40)])))
        for us, c in steps:
            out = res(months_reference, us, c)
            if engine is not None and abs(c) <= 3000:
                assert out == res(lambda: engine(us_dt(us), c)), (us, c)
            w.row("mstep", us, c, out)


if __name__ == "__main__":
    main()
