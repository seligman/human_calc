"""Vectors for tests/engine/test_value.c: calc/value.py (Value._shown, _fixed, as_int,
is_valid, as_value, to_string, as_date, as_base).

Rows (columns after the case name):
    shown     x, Value._shown(x)                        (str or the exception)
    fixed     x, places, Value._fixed(x, places)
    as_int    x, Value.as_int(x)
    valid     x, Value(x).is_valid()
    as_value  s, Value.as_value(s).value, or null for None
    tostr     payload, modifier, Value(payload, Modifier(modifier, modifier)).to_string()
              (modifier null: no modifier; the result is a str or the exception)
    tostr_en  the same for the "english" unit (needs Word.to_english, src/word.c, U13)
    base      x, base, Value.as_base(x, base).to_string()
    date      us, is_time, Value.as_date(datetime, is_time).to_string()

x is an int or a float (vecutil.enc); payload is x or a DateValue written as
{"date": microseconds since 0001-01-01, "time": is_time}.
"""

import vecutil

import math
import sys
from datetime import datetime, timedelta

EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)
MAX_US = (datetime.max - EPOCH0) // US
DBL_MAX = sys.float_info.max


def us_dt(us):
    return EPOCH0 + timedelta(microseconds=us)


def shown_edges():
    out = list(vecutil.edge_doubles())
    out += [999999999999998.0, 999999999999999.0, 99999999999999.5, 1.5e-14, 1e-15,
            1.23456789e-15, 9007199254740991.5, 2.0 ** 53, 2.0 ** 64, 6.022e23, 1 / 3e8,
            1.123456789, 2.123456789, 1000.1234567, 123456.785, math.pi, 1 / 3, 0.1 + 0.2,
            0.3 / 0.1, 1.99999999999, 1.9999999999999, 0.09999999999, 0.1000000001,
            0.10000000001, 0.099999999999, -0.1, -0.09999999, 9.9999999e-15, 1e-13, 1e-20,
            1e-21, 1.0000000000000002, 0.9999999999999999, 12345678901234.56, 123456789012345.6,
            1234567890123456.7, 2.675, 1.005, 0.125, 0.0000001, 0.00000005, 5e-8, 4.9999999e-8,
            -1e-15, 1e-6, 1e-7, 1e-11, 1e-12, 1e-16, 1e-19, 1e-23, 1e-14 * 0.9999999999999999,
            1.7976931348623157e308, 8.98846567431158e307, 1e308, 4.5, 5.5, -0.5, -1.5]
    for d in range(1, 18):
        out.append(float("9" * d) + 0.5)
        out.append(float("9" * d) + 0.4)
        out.append(float("1" + "0" * d))
        out.append(-float("1" + "0" * d) - 0.5)
    for e in range(-24, 3):
        out.append(10.0 ** e)
        out.append(10.0 ** e * 0.99999999)
        out.append(10.0 ** e * 1.00000001)
    return out


def int_edges():
    big = int(DBL_MAX)
    return [0, 1, -1, 2, 999, 1000, -1000, 123456789, 10 ** 15, 10 ** 16, 2 ** 53, 2 ** 63 - 1,
            -(2 ** 63), 2 ** 63, 2 ** 64, 10 ** 30, -(10 ** 30), big, big + 1, -big, -big - 1,
            2 ** 1024, 10 ** 308, 10 ** 309, 10 ** 399, 10 ** 400, 10 ** 401, 10 ** 4299,
            10 ** 4300 - 1, 10 ** 4300, 10 ** 4301, 170 * 10 ** 200]


def rand_number(rng):
    r = rng.random()
    sign = -1 if rng.random() < 0.3 else 1
    if r < 0.30:
        return sign * 10.0 ** rng.uniform(-25, 25)
    if r < 0.45:
        places = rng.randint(0, 6)
        x = round(rng.uniform(0, 10 ** rng.randint(0, 9)), places)
        if rng.random() < 0.3:
            x = float(f"{x:.{places}f}5")
        return sign * x
    if r < 0.55:
        return sign * rng.randint(0, 10 ** rng.randint(0, 9))
    if r < 0.60:
        return sign * rng.randint(0, 10 ** rng.randint(15, 320))
    if r < 0.70:
        return rng.choice(shown_edges())
    if r < 0.80:
        return vecutil.rand_double(rng)
    if r < 0.90:
        n = rng.randint(0, 10 ** rng.randint(0, 15))
        return sign * (n + rng.choice((1, -1)) * 10.0 ** rng.uniform(-14, -5))
    if r < 0.95:
        return sign * rng.uniform(0, 0.1) * 10.0 ** -rng.randint(0, 20)
    return sign * float(rng.randint(0, 10 ** 6)) / rng.choice((3, 7, 9, 11, 100, 1000))


def rand_us(rng):
    r = rng.random()
    if r < 0.4:
        return rng.randint(0, MAX_US)
    if r < 0.6:
        # Dates at midnight and whole seconds
        us = rng.randint(0, MAX_US)
        return us - us % (86400 * 10 ** 6 if rng.random() < 0.5 else 10 ** 6)
    if r < 0.8:
        # Times of day (day 0) and durations past a day
        return rng.randint(0, 86400 * 10 ** 6 * rng.choice((1, 1, 2, 1000, 3652059)) - 1)
    edges = [0, 1, 999999, 10 ** 6, 86400 * 10 ** 6 - 1, 86400 * 10 ** 6, MAX_US, MAX_US - 1,
             (datetime(999, 12, 31, 23, 59, 59) - EPOCH0) // US,
             (datetime(1000, 1, 1) - EPOCH0) // US, (datetime(2021, 7, 1, 12, 34, 56) - EPOCH0) // US,
             (datetime(9999, 12, 31) - EPOCH0) // US + 1, 3652058 * 86400 * 10 ** 6 + 86399999999,
             (datetime(2020, 2, 29, 0, 0, 0, 250000) - EPOCH0) // US]
    return rng.choice(edges)


def main():
    sys.dont_write_bytecode = True
    vecutil.import_calc()
    from calc.value import Value
    from calc.modifier import Modifier, _lookup, _data
    from calc.date_value import DateValue

    def payload(v):
        if isinstance(v, DateValue):
            return {"date": (v.value - EPOCH0) // US, "time": v.is_time}
        return v

    def to_string(v, unit):
        return Value(v, None if unit is None else Modifier(unit, unit)).to_string()

    units = sorted(set(_lookup.values()))
    with vecutil.Writer("value") as w:
        rng = w.rng
        per_unit = w.pick(quick=10, full=300)
        many = w.pick(quick=3000, full=200000)

        numbers = shown_edges() + int_edges()
        for x in numbers:
            w.call("shown", Value._shown, x)
        for _ in range(many):
            w.call("shown", Value._shown, rand_number(rng))

        for x in numbers:
            for places in (0, 2, 4, 6):
                w.call("fixed", Value._fixed, x, places)
        for _ in range(many):
            w.call("fixed", Value._fixed, rand_number(rng), rng.choice((0, 2, 4, 6)))

        near = []
        for n in (0, 1, -1, 2, 3, 10, 1000, 2 ** 52, 2 ** 53, 10 ** 15):
            for d in (0.0, 1e-16, 2e-16, 4e-16, 8e-16, 1e-15, 1e-12, 1e-9, 9.9e-7, 1e-6, 1.1e-6,
                      1e-5, 0.4, 0.5, 0.6, 0.9999999, 1 - 1e-12):
                near += [n + d, n - d, -(n + d)]
        # The two edges of the tolerance: the 1e-6 cap (scales above about 1.1e9) and a
        # difference exactly 4 * eps * max(1, abs(value)) (1 - 2**-50)
        for n in (1500000000, 2 ** 31 - 1, 1200000000, 3000000000, 7 * 10 ** 9, 5 * 10 ** 8,
                  3 * 10 ** 8, 10 ** 9):
            u = math.ulp(float(n))
            for k in range(1, 17):
                near += [n + k * u, n - k * u, -(n + k * u)]
        for n in (0, 1, 2, 3, 4, 7, 1000):
            for e in (49, 50, 51, 52):
                d = 2.0 ** -e * max(1, n)
                near += [n + d, n - d, -(n - d), 0.5 * n + d]
        for x in numbers + near:
            w.call("as_int", Value.as_int, x)
        for _ in range(many // 2):
            x = rand_number(rng)
            w.call("as_int", Value.as_int, x)

        for x in numbers:
            w.row("valid", x, Value(x).is_valid())

        texts = ["", ",", ".", "1", "12", "1.5", "1,5", "1,2,3", "1.2.3", ".5", "5.", "..5",
                 "1..2", "00012", "1e5", " 1", "1 ", "-1", "+1", "0x10", "1_0", "12\n", "12\n\n",
                 "\n12", "1.\n", ",\n", "\uff11", "1\u0661", "1" * 400, "1" * 308, "1" * 309,
                 "1" * 310, "9" * 309 + "." + "9" * 20, "0." + "0" * 400 + "1", "1" * 10000,
                 "1," * 5000, "," * 100, "." * 3, "1,000,000.25", "3.14159265358979323846264338",
                 "0.1", "0.30000000000000004", "123456789012345678901234567890"]
        for _ in range(w.pick(quick=600, full=30000)):
            texts.append("".join(rng.choice("0123456789,.") for _ in range(rng.randint(0, 14))))
            if rng.random() < 0.1:
                texts.append(vecutil.rand_text(rng, 6, pool=list("0123456789,.") * 5 +
                                               list("e-+ \n\tx\u0660")))
        for s in texts:
            v = Value.as_value(s)
            w.row("as_value", s, None if v is None else v.value)

        dates = [DateValue(us_dt(rand_us(rng)), rng.random() < 0.4)
                 for _ in range(w.pick(quick=60, full=3000))]
        for unit in units + [None]:
            case = "tostr_en" if unit is not None and _data[unit.lower()][0] == "english" else "tostr"
            values = [rand_number(rng) for _ in range(per_unit)]
            values += [rng.choice(dates) for _ in range(2)]
            if unit is None or unit.startswith("\ue001") or case == "tostr_en":
                values += [0, -0.0, 0.0, -0.4, -0.5, 0.5, 1.5, 2.5, -2.5, 16, -16, 255.99999999,
                           -255.5, 1e300, -1e300, 2 ** 70, -(2 ** 70), float("inf"), float("nan"),
                           0.1 + 0.2, 1e-30, -1e-30, 123.456]
                values += [rand_number(rng) for _ in range(per_unit * 3)]
            if unit is not None and _data[unit.lower()][0] == "currency":
                values += [1.005, 2.675, 0.125, -1.005, 2.5, -0.001, -0.00004, 0.0000004,
                           12345678901234.56, 1.7976931348623157e308, -0.0, 0.5, 1.5, -0.5,
                           float("inf"), float("nan"), 10 ** 400, 999.995, 0.995, 1e15 + 0.3]
            for v in values:
                w.row(case, payload(v), unit, vecutil.result(to_string, v, unit))

        for _ in range(w.pick(quick=600, full=40000)):
            d = DateValue(us_dt(rand_us(rng)), rng.random() < 0.5)
            w.row("tostr", payload(d), "\ue001date", vecutil.result(to_string, d, "\ue001date"))

        for x in int_edges() + [rand_number(rng) for _ in range(w.pick(quick=200, full=10000))]:
            for base in ("hex", "oct", "bin"):
                w.row("base", x, base, vecutil.result(lambda: Value.as_base(x, base).to_string()))

        for _ in range(w.pick(quick=300, full=20000)):
            us = rand_us(rng)
            is_time = rng.random() < 0.5
            w.row("date", us, is_time,
                  vecutil.result(lambda: Value.as_date(us_dt(us), is_time).to_string()))


if __name__ == "__main__":
    main()
