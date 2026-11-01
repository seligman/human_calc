"""Vectors for tests/engine/test_date_value.c: calc/date_value.py (DateValue).

Datetimes are microseconds since 0001-01-01 (ints). Rows (columns after the case name):
    days       us, is_time, DateValue(us, is_time).days()
    helper     us, count, DateValue._add_months_helepr(us, count) (us, or the exception)
    add_month  self_us, months, start_us or null, day or null, DateValue.add_month(...)
    add_year   self_us, years, start_us or null, day or null, DateValue.add_year(...)
    diff       a_us, b_us, [diff_days, diff_span, diff_months, diff_years] of DateValue(a)
               against DateValue(b) (each a value or the exception; diff_span a 4-list)
    add        self_us, is_time, other, DateValue + other: [us, is_time] or the exception
    sub        self_us, is_time, other, DateValue - other: [us, is_time], ["tuple", amount,
               unit], or the exception
other is a Value: {"v": payload, "m": unit or null, "x": exact_days or null, "xs": span or
null}, payload an int, a float or {"date": us, "time": is_time}; Value(payload,
Modifier(m, m)) with exact_days and span set as given.
"""

import vecutil

import calendar
import sys
from datetime import datetime, timedelta

EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)
MAX_US = (datetime.max - EPOCH0) // US
DAY = 86400 * 10 ** 6


def us_dt(us):
    return EPOCH0 + timedelta(microseconds=us)


def dt_us(dt):
    return (dt - EPOCH0) // US


def main():
    sys.dont_write_bytecode = True
    vecutil.import_calc()
    from calc.value import Value
    from calc.modifier import Modifier
    from calc.date_value import DateValue

    with vecutil.Writer("date_value") as w:
        rng = w.rng
        n = w.pick(quick=300, full=12000)
        # Python steps months one at a time, so a count of k costs k datetime() calls: the
        # quick tier keeps most counts and date distances small
        p_big = w.pick(quick=0.04, full=0.08)

        def rand_date_us():
            r = rng.random()
            if r < 0.35:
                return rng.randint(0, MAX_US)
            if r < 0.55:
                y = rng.choice((1, 2, 4, 99, 100, 400, 1900, 1999, 2000, 2001, 2020, 2021, 2024,
                                2100, 9998, 9999)) if rng.random() < 0.6 else rng.randint(1, 9999)
                m = rng.randint(1, 12)
                d = min(rng.choice((1, 28, 29, 30, 31, rng.randint(1, 31))),
                        calendar.monthrange(y, m)[1])
                t = rng.choice((0, 0, rng.randint(0, DAY - 1), DAY - 1, 12 * 3600 * 10 ** 6))
                return dt_us(datetime(y, m, d)) + t
            if r < 0.7:
                us = dt_us(datetime(rng.randint(1990, 2030), rng.randint(1, 12), rng.randint(1, 28)))
                return us
            if r < 0.85:
                return rng.randint(0, DAY * rng.choice((1, 2, 30, 1000, 3652059)) - 1)
            return rng.choice((0, 1, MAX_US, MAX_US - 1, DAY - 1, DAY, 3652058 * DAY + DAY - 1,
                               dt_us(datetime(2021, 7, 1, 12, 34, 56)), dt_us(datetime(2020, 2, 29)),
                               dt_us(datetime(2021, 1, 31)), dt_us(datetime(2021, 3, 31)),
                               dt_us(datetime(9999, 12, 1)), dt_us(datetime(1, 1, 31))))

        def rand_count():
            if rng.random() >= p_big:
                return rng.randint(-30, 30) if rng.random() < 0.7 else rng.randint(-1500, 1500)
            r = rng.random()
            if r < 0.5:
                return rng.randint(-30, 30)
            if r < 0.8:
                return rng.randint(-130000, 130000)
            if r < 0.95:
                return rng.choice((0, 1, -1, 11, 12, 13, -11, -12, -13, 119987, 119988, 119989,
                                   -119987, -119988, -119989, 120000, -120000, 120001, -120001,
                                   2 ** 31, -(2 ** 31), 2 ** 62, -(2 ** 62)))
            return rng.choice((2 ** 63 - 1, -(2 ** 63), 2 ** 63 - 13, -(2 ** 63) + 13))

        for _ in range(n):
            us = rand_date_us()
            t = rng.random() < 0.5
            w.row("days", us, t, DateValue(us_dt(us), t).days())
        for us in (0, 1, DAY - 1, DAY, MAX_US, 3652058 * DAY + DAY - 1, 3652058 * DAY + DAY - 2):
            for t in (False, True):
                w.row("days", us, t, DateValue(us_dt(us), t).days())

        def helper(us, count):
            dv = DateValue(us_dt(0), False)
            return dt_us(dv._add_months_helepr(us_dt(us), count))

        for _ in range(n):
            us, c = rand_date_us(), rand_count()
            w.row("helper", us, c, vecutil.result(helper, us, c))

        def months_call(name, self_us, count, start_us, day):
            dv = DateValue(us_dt(self_us), False)
            start = None if start_us is None else us_dt(start_us)
            fn = dv.add_month if name == "add_month" else dv.add_year
            return dt_us(fn(count, start, day))

        for name in ("add_month", "add_year"):
            for _ in range(n):
                self_us = rand_date_us()
                count = rand_count() if name == "add_month" else rng.choice(
                    (rng.randint(-20, 20), rng.randint(-120, 120)) if rng.random() >= p_big else
                    (rng.randint(-10000, 10000), rng.randint(-12000, 12000)))
                if rng.random() < 0.5:
                    start_us, day = None, None
                else:
                    start_us = rand_date_us()
                    day = rng.choice((us_dt(start_us).day, us_dt(start_us).day, 1, 28, 29, 30, 31))
                w.row(name, self_us, count, start_us, day,
                      vecutil.result(months_call, name, self_us, count, start_us, day))

        def diffs(a_us, b_us):
            a, b = DateValue(us_dt(a_us), False), DateValue(us_dt(b_us), False)
            return [vecutil.result(a.diff_days, b), vecutil.result(lambda: list(a.diff_span(b))),
                    vecutil.result(a.diff_months, b), vecutil.result(a.diff_years, b)]

        pairs = []
        for _ in range(n):
            a = rand_date_us()
            r = rng.random()
            if r < p_big:
                b = rand_date_us()
            elif r < 0.6:
                b = max(0, min(MAX_US, a + rng.randint(-400, 400) * DAY + rng.choice((0, rng.randint(-DAY, DAY)))))
            elif r < 0.8:
                ad = us_dt(a)
                y = min(9999, max(1, ad.year + rng.randint(-3, 3)))
                m = rng.randint(1, 12)
                try:
                    b = dt_us(ad.replace(year=y, month=m))
                except ValueError:
                    b = dt_us(datetime(y, m, 28, ad.hour))
            else:
                b = a + rng.choice((0, 1, -1, DAY, -DAY))
                b = max(0, min(MAX_US, b))
            pairs.append((a, b))
        pairs += [(dt_us(datetime(2021, 3, 1)), dt_us(datetime(2021, 1, 31))),
                  (dt_us(datetime(2020, 2, 29)), dt_us(datetime(2021, 2, 28))),
                  (dt_us(datetime(2021, 12, 31)), dt_us(datetime(2022, 1, 1))),
                  (0, MAX_US), (MAX_US, 0), (0, 0)]
        for a, b in pairs:
            w.row("diff", a, b, diffs(a, b))

        def near(a):
            # Python's diff_span steps one month per iteration, so far-apart pairs are slow:
            # most pairs are within a few years
            if rng.random() < p_big:
                return rand_date_us()
            return max(0, min(MAX_US, a + rng.randint(-2000, 2000) * DAY +
                              rng.choice((0, rng.randint(-DAY, DAY)))))

        def payload(v):
            if isinstance(v, DateValue):
                return {"date": dt_us(v.value), "time": v.is_time}
            return v

        def rand_payload(self_us):
            r = rng.random()
            if r < 0.3:
                return rng.randint(-60, 60)
            if r < 0.55:
                return rng.choice((1, -1)) * rng.uniform(0, 60)
            if r < 0.65:
                return rng.choice((0, -0.0, 0.0, 0.5, -0.5, 1 / 3, 1.5, 2.5, 1e-9, 1e-10,
                                   0.99999999999, 1.00000000001, 11.999999999999, 1e8, 1e8 + 1,
                                   -1e8, 3652059, 3652060, 119988, 1e15, 1e300, -1e300,
                                   float("inf"), float("-inf"), float("nan"), 10 ** 20,
                                   -(10 ** 20), 2 ** 63, 0.0000001, 1e-6, 5e-7, 1 + 1e-8,
                                   2 - 5e-7, 3 + 2e-9, 0.99999999, 5.0000000005, 7 - 1e-10,
                                   1 + 1e-9, 1 - 1e-9, -2 - 3e-7, 4 + 9.9e-7))
            if r < 0.85:
                return rng.choice((1, -1)) * rng.uniform(0, 10 ** rng.randint(1, 7))
            return DateValue(us_dt(near(self_us)), rng.random() < 0.3)

        def rand_other(self_us):
            v = rand_payload(self_us)
            m = rng.choice((None, None, "days", "hours", "months", "years", "months", "years",
                            "km", "USD", "\ue001date", "\ue001hex"))
            x = xs = None
            r = rng.random()
            if r < 0.2:
                a = rand_date_us()
                b = near(a)
                da, db = DateValue(us_dt(a), False), DateValue(us_dt(b), False)
                x = da.diff_days(db)
                xs = da.diff_span(db)
            elif r < 0.3:
                x = rng.uniform(-1000, 1000)
            elif r < 0.35:
                a = rand_date_us()
                b = near(a)
                xs = DateValue(us_dt(a), False).diff_span(DateValue(us_dt(b), False))
            return v, m, x, xs

        def realistic():
            # What Op_Sub makes of two dates: Value(amount, unit) with exact_days and span
            a_us = rand_date_us()
            a = DateValue(us_dt(a_us), False)
            b = DateValue(us_dt(near(a_us)), False)
            amount, unit = a - Value(b)
            x = a.diff_days(b)
            xs = a.diff_span(b)
            if rng.random() < 0.3:
                amount, x = -amount, -x
            return amount, unit, x, xs

        def build(v, m, x, xs):
            val = Value(v, None if m is None else Modifier(m, m))
            val.exact_days = x
            val.span = xs
            return val

        def add_result(self_us, t, spec):
            dv = DateValue(us_dt(self_us), t)
            r = dv + build(*spec)
            return [dt_us(r.value), r.is_time]

        def sub_result(self_us, t, spec):
            dv = DateValue(us_dt(self_us), t)
            r = dv - build(*spec)
            if isinstance(r, tuple):
                return ["tuple", r[0], r[1]]
            return [dt_us(r.value), r.is_time]

        def enc_spec(spec):
            v, m, x, xs = spec
            return {"v": payload(v), "m": m, "x": x, "xs": None if xs is None else list(xs)}

        # Dates exactly a whole number of months, 1.5 years (of 365 days) or a day apart
        exact = [(datetime(2021, 2, 15), datetime(2021, 1, 15)),
                 (datetime(2021, 3, 31), datetime(2021, 2, 28)),
                 (datetime(2021, 3, 28), datetime(2021, 2, 28)),
                 (datetime(2021, 12, 15, 6), datetime(2021, 11, 15, 6)),
                 (datetime(2020, 7, 1, 12), datetime(2019, 1, 1)),
                 (datetime(2022, 7, 2, 12), datetime(2021, 1, 1)),
                 (datetime(2022, 7, 2, 11, 59, 59), datetime(2021, 1, 1)),
                 (datetime(2021, 1, 1), datetime(2020, 12, 31)),
                 (datetime(2021, 1, 31), datetime(2020, 12, 31)),
                 (datetime(2021, 2, 1), datetime(2020, 12, 31)),
                 (datetime(2023, 1, 1), datetime(2021, 1, 1)),
                 (datetime(2022, 12, 31), datetime(2021, 1, 1))]
        for a, b in exact:
            for x, y in ((a, b), (b, a)):
                for t in (False, True):
                    spec = (DateValue(y, t), None, None, None)
                    w.row("sub", dt_us(x), t, enc_spec(spec),
                          vecutil.result(sub_result, dt_us(x), t, spec))

        # Month counts near whole numbers: round() within 1e-9, else math.trunc()
        for v in (1 + 1e-8, 2 - 5e-7, 3 + 2e-9, 0.99999999, 5.0000000005, 7 - 1e-10, 1 + 1e-9,
                  1 - 1e-9, -2 - 3e-7, 4 + 9.9e-7, 1 + 9.99e-10, 1 - 9.99e-10, -1 + 2e-9):
            for m in ("months", "years"):
                for t in (False, True):
                    self_us = dt_us(datetime(2021, 1, 31, 10, 30))
                    spec = (v, m, None, None)
                    w.row("add", self_us, t, enc_spec(spec),
                          vecutil.result(add_result, self_us, t, spec))
                    w.row("sub", self_us, t, enc_spec(spec),
                          vecutil.result(sub_result, self_us, t, spec))

        for name, fn in (("add", add_result), ("sub", sub_result)):
            for i in range(n * 3):
                self_us = rand_date_us()
                t = rng.random() < 0.3
                spec = realistic() if i % 4 == 0 else rand_other(self_us)
                w.row(name, self_us, t, enc_spec(spec), vecutil.result(fn, self_us, t, spec))


if __name__ == "__main__":
    main()
