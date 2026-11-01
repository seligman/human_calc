"""Vectors for tests/engine/test_function.c: calc/function.py (Function).

Rows (columns after the case name):
    call         name, arg, Function(name).call(engine, arg): a result or the exception
    whole        x, use_ceil, Function._whole(x, math.ceil if use_ceil else math.floor)
    trig         which ("sin", "cos", "tan"), degrees, Function._trig(math.<which>, degrees,
                 <the exact table of func_<which>>)
    accepts      name, unit or null, Function(name).accepts(Value(1.0, Modifier(unit, unit)))
    can          name, next, Function(name).can_handle(engine, None) with next linked after
                 it: next is null (nothing), or {"cls": "Value", "m": unit or null}, or
                 {"cls": "Paren"} / {"cls": "String"} / {"cls": "Function"}
    plain        name, name in Function.PLAIN_ONLY
    as_function  text, Function.as_function(text).value, or null for None
    names        [the keys of Function.cached, in order]

The call, can and as_function rows start with every call the engine makes while it runs the
inputs of human_calc.py test (an arg there also has "o", the modifier's orig_value).

arg is a Value: {"v": payload, "m": unit or null, "x": exact_days or null, "xs": span or null},
payload an int, a float or {"date": us, "time": is_time} (microseconds since 0001-01-01), built
as Value(payload, Modifier(m, m)) with exact_days and span set. A result is {"v": payload,
"m": modifier.value or null, "o": modifier.orig_value or null, "same": the result's modifier
is arg's modifier object, "x": exact_days or null, "xs": span or null, "s": to_string() or
the exception}.
"""

import vecutil

import math
import sys
from datetime import datetime, timedelta

EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)
MAX_US = (datetime.max - EPOCH0) // US
DBL_MAX = sys.float_info.max

NAMES_EXTRA = ["", "foo", "Sin", "SQRT", "log1", "log10 ", "lg", "abs2", "exp", "e", "pi",
               "ceil\u0130ng", "s\u0130n", "S\u0131N", "\uff53\uff49\uff4e", "\u017fin",
               "\u017fqrt", "co\u017f", "LOG\u2081\u2080", "Log2", "LN", "Value", "VALUE",
               "fourthRoot", "CBRT", "\u0391bs", "ab\u0405", "a\u0299s", "ce\u0130l"]


def us_dt(us):
    return EPOCH0 + timedelta(microseconds=us)


def suite_inputs():
    """The inputs of human_calc.py test, in its order: test_cases.txt, then the README.md
    examples (the two web time-zone lines use the real clock and are left out)."""
    import json
    import os
    import re

    out = []
    with open(os.path.join(vecutil.ROOT, "test_cases.txt"), encoding="utf-8") as f:
        for row in f:
            row = row.split("#")[0].strip()
            if row:
                out.append(json.loads(row)[0])
    path = os.path.join(vecutil.ROOT, "README.md")
    if os.path.isfile(path):
        in_section, value = False, None
        with open(path, encoding="utf-8") as f:
            for row in f:
                row = row.strip()
                if row in {"", "```", "<pre>", "</pre>"}:
                    in_section = False
                if in_section:
                    if value is None:
                        value = re.sub("&#(?P<chr>[0-9]+);", lambda m: chr(int(m.group("chr"))), row)
                    else:
                        out.append(value)
                        value = None
                if row.startswith("# "):
                    in_section = True
    return out


def main():
    sys.dont_write_bytecode = True
    vecutil.import_calc()
    from calc.function import Function
    from calc.modifier import Modifier, _lookup, _data
    from calc.value import Value
    from calc.date_value import DateValue
    from calc.paren import Paren
    from calc.string import String
    from calc.token import Token

    if Function.cached is None:
        Function.cache_functions()
    names = list(Function.cached)
    engine = vecutil.test_engine()
    units = sorted(set(_lookup.values()))
    display = sorted(u for u in units if _data[u.lower()][0] in ("base", "english"))
    date_unit = Token.UNPRINTABLE + "date"

    def payload(v):
        if isinstance(v, DateValue):
            return {"date": (v.value - EPOCH0) // US, "time": v.is_time}
        return v

    def make(v, unit, x=None, xs=None):
        if unit is None:
            ret = Value(v)
        else:
            ret = Value(v, Modifier(unit, unit if unit != date_unit else "date"))
        ret.exact_days = x
        ret.span = xs
        return ret

    def arg_desc(value, orig=False):
        ret = {"v": vecutil.Raw(vecutil.enc(payload(value.value))),
               "m": None if value.modifier is None else value.modifier.value,
               "x": value.exact_days,
               "xs": None if value.span is None else list(value.span)}
        if orig and value.modifier is not None:
            ret["o"] = value.modifier.orig_value
        return ret

    def result_desc(ret, arg):
        if isinstance(ret, vecutil.Exc):
            return ret
        mod = ret.modifier
        return {"v": vecutil.Raw(vecutil.enc(payload(ret.value))),
                "m": None if mod is None else mod.value,
                "o": None if mod is None else mod.orig_value,
                "same": mod is not None and mod is arg.modifier,
                "x": ret.exact_days,
                "xs": None if ret.span is None else list(ret.span),
                "s": vecutil.result(ret.to_string)}

    # What the engine itself does while it runs the test suite: every Function.call (from
    # handle and from Paren.handle), every can_handle and every as_function, deduplicated
    suite = {}
    orig_call, orig_can, orig_as = Function.call, Function.can_handle, Function.as_function

    def note(*row):
        suite.setdefault(vecutil.dumps(vecutil.enc(list(row))), row)

    def call(self, engine_, value):
        arg = arg_desc(value, orig=True)
        try:
            ret = orig_call(self, engine_, value)
        except Exception as e:
            note("call", self.value, arg, vecutil.Exc(vecutil.exc_name(e)))
            raise
        note("call", self.value, arg, result_desc(ret, value))
        return ret

    def can_handle(self, engine_, other):
        ret = orig_can(self, engine_, other)
        nxt = self.next
        if nxt is None:
            desc = None
        elif isinstance(nxt, Value):
            desc = {"cls": "Value", "m": None if nxt.modifier is None else nxt.modifier.value}
        else:
            desc = {"cls": "String"}
        note("can", self.value, desc, ret)
        return ret

    def as_function(value):
        ret = orig_as(value)
        note("as_function", value, None if ret is None else ret.value)
        return ret

    Function.call, Function.can_handle = call, can_handle
    Function.as_function = staticmethod(as_function)
    try:
        suite_engine = vecutil.test_engine()
        for line in suite_inputs():
            suite_engine.calc(line)
    finally:
        Function.call, Function.can_handle = orig_call, orig_can
        Function.as_function = staticmethod(orig_as)

    with vecutil.Writer("function") as w:
        rng = w.rng
        w.row("names", names)
        for row in suite.values():
            w.row(*row)

        def rand_float():
            r = rng.random()
            sign = -1 if rng.random() < 0.4 else 1
            if r < 0.35:
                return sign * 10.0 ** rng.uniform(-12, 12)
            if r < 0.5:
                return sign * rng.uniform(0, 2)
            if r < 0.65:
                return sign * float(rng.randint(0, 720)) * rng.choice((1, 1, 0.5, 0.25, 1.5))
            if r < 0.75:
                return vecutil.rand_double(rng)
            if r < 0.85:
                n = rng.randint(0, 10 ** rng.randint(0, 16))
                return sign * (n + rng.choice((1, -1)) * 10.0 ** rng.uniform(-17, -5))
            return sign * 10.0 ** rng.uniform(-320, 308)

        def rand_int():
            r = rng.random()
            sign = -1 if rng.random() < 0.4 else 1
            if r < 0.6:
                return sign * rng.randint(0, 1000)
            if r < 0.9:
                return sign * rng.randint(0, 10 ** rng.randint(1, 30))
            return sign * rng.randint(0, int(DBL_MAX))

        def rand_date():
            us = rng.randint(0, MAX_US)
            if rng.random() < 0.5:
                us -= us % (86400 * 10 ** 6)
            return DateValue(us_dt(us), rng.random() < 0.3)

        edges = [0, 1, -1, 2, 27, -27, 64, 1000, 2 ** 53, 2 ** 53 + 1, -(2 ** 63), 2 ** 63,
                 2 ** 200, int(DBL_MAX), -int(DBL_MAX), 0.0, -0.0, 0.5, -0.5, 1.0, -1.0, 2.5,
                 -2.5, 3.5, 27.0, -27.0, 8.0, 1e-300, 5e-324, -5e-324, 1e300, DBL_MAX, -DBL_MAX,
                 0.2 ** -2, 0.3 / 0.1, 2.9999999999999996, 1.0000000000000002,
                 0.9999999999999999, 4503599627370495.5, 4503599627370496.5, 2.0 ** 52 + 0.5,
                 2.0 ** 53, 2.0 ** 53 - 1, -(2.0 ** 53), 9007199254740993.0, 1e16, 1e-6, 1e-7,
                 1.000001, 1.0000001, 999999999.9999999, 1e9 + 1e-6, 1e9 + 2e-6, 30, 45, 60,
                 90, 180, 270, 360, 450, -90, -180, -270, -360, 30.0, 90.0, 1e22, 1e300 * 1.5,
                 0.1, 0.2, 0.3, math.pi, math.e, 0.49999999999999994, 1.5, 2.0, 4.0, 16.0,
                 81.0, 0.0625, 1e-320, 123456789.0, float("inf"), float("-inf"), float("nan")]
        for e in range(-1074, 1024, 61):
            edges.append(2.0 ** e)

        def rand_unit():
            r = rng.random()
            if r < 0.55:
                return None
            if r < 0.8:
                return rng.choice(display)
            return rng.choice(units)

        def rand_arg():
            r = rng.random()
            if r < 0.06:
                return make(rand_date(), date_unit)
            if r < 0.3:
                v = rand_int()
            elif r < 0.5:
                v = rng.choice(edges)
            else:
                v = rand_float()
            x = None
            xs = None
            if rng.random() < 0.1:
                x = rng.choice((-12.25, 3.5, 0.0, -0.0, 1e10, -1.5))
                if rng.random() < 0.5:
                    xs = (rng.randint(-30, 30), rng.uniform(-40, 40), rng.randint(-30, 30),
                          rng.uniform(-40, 40))
            return make(v, rand_unit(), x, xs)

        def emit_call(name, arg):
            ret = vecutil.result(Function(name).call, engine, arg)
            w.row("call", name, arg_desc(arg), result_desc(ret, arg))

        logs = {"log", "log10", "log2", "ln"}
        per_name = w.pick(quick=120, full=5000)
        for name in names:
            for v in edges:
                if name in logs and isinstance(v, int) and abs(v) > DBL_MAX:
                    continue
                emit_call(name, make(v, None))
            for unit in display + ["km", "Euro", None]:
                emit_call(name, make(rng.choice(edges[:40]), unit))
                emit_call(name, make(rand_float(), unit))
            emit_call(name, make(rand_date(), date_unit))
            emit_call(name, make(rand_date(), None))
            emit_call(name, make(-5.0, "km", -12.25, (1, 2.5, 3, -4.5)))
            emit_call(name, make(5, "\ue001hex", 12.25, None))
            # abs: only a value below zero negates exact_days
            for v in (0, 0.0, -0.0, -5e-324, 5e-324, -1, 1):
                emit_call(name, make(v, None, 3.5, None))
            for _ in range(per_name):
                emit_call(name, rand_arg())

        # cbrt's Newton step takes ret ** 3 and ret ** 2 through pow(), never ret * ret
        for _ in range(w.pick(quick=4000, full=300000)):
            r = rng.random()
            if r < 0.5:
                v = 10.0 ** rng.uniform(-300, 300)
            elif r < 0.8:
                v = float(rng.randint(1, 10 ** 6)) ** 3 * rng.choice((1, 1, 1.0000000000000002))
            else:
                v = vecutil.rand_double(rng)
            emit_call("cbrt", make(v if rng.random() < 0.7 else -v, None))

        whole = list(edges)
        for n in (0, 1, -1, 2, 3, 10, 1000, 2 ** 31, 2 ** 52, 2 ** 53, 10 ** 15):
            for d in (0.0, 1e-16, 2e-16, 4e-16, 8e-16, 1e-15, 1e-12, 1e-9, 9.9e-7, 1e-6,
                      1.1e-6, 1e-5, 0.4, 0.5, 0.6, 0.9999999):
                whole += [n + d, n - d, -(n + d), -(n - d)]
        for n in (1500000000, 2 ** 31 - 1, 1200000000, 3000000000, 7 * 10 ** 9, 10 ** 9):
            u = math.ulp(float(n))
            for k in range(1, 17):
                whole += [n + k * u, n - k * u, -(n + k * u)]
        for n in (0, 1, 2, 3, 4, 7, 1000):
            for e in (49, 50, 51, 52):
                d = 2.0 ** -e * max(1, n)
                whole += [n + d, n - d, -(n - d), 0.5 * n + d]
        whole += [rand_float() for _ in range(w.pick(quick=1500, full=60000))]
        whole += [rand_int() for _ in range(w.pick(quick=100, full=3000))]
        whole += [2 ** 1024, -(2 ** 1024), 2 ** 1100, int(DBL_MAX) + 1]
        for x in whole:
            for use_ceil in (False, True):
                w.row("whole", x, use_ceil,
                      vecutil.result(Function._whole, x, math.ceil if use_ceil else math.floor))

        tables = {"sin": (math.sin, (0.0, 1.0, 0.0, -1.0)),
                  "cos": (math.cos, (1.0, 0.0, -1.0, 0.0)),
                  "tan": (math.tan, (0.0, None, 0.0, None))}
        trig = list(edges)
        for k in range(-12, 13):
            trig += [90 * k, 90.0 * k, 90.0 * k + 1e-13, 90.0 * k - 1e-9, 45.0 * k, 30.0 * k,
                     360.0 * 10 ** abs(k) * (1 if k >= 0 else -1)]
        trig += [1e300, -1e300, 1e20, 2.0 ** 60, 720.0 + 90, -1e-20, 1e-20, 359.99999999999994,
                 -359.99999999999994, 89.99999999999999, 90.00000000000001, 6e-17]
        trig += [rand_float() for _ in range(w.pick(quick=1500, full=60000))]
        trig += [rand_int() for _ in range(w.pick(quick=100, full=3000))]
        for x in trig:
            for which, (fn, exact) in tables.items():
                w.row("trig", which, x, vecutil.result(Function._trig, fn, x, exact))

        for name in names + NAMES_EXTRA[:3]:
            for unit in display + [None, "km", "Euro", date_unit, rng.choice(units),
                                   rng.choice(units)]:
                w.row("accepts", name, unit, Function(name).accepts(make(1.0, unit)))
            w.row("plain", name, name in Function.PLAIN_ONLY)

        for name in names:
            nexts = [None, {"cls": "Paren"}, {"cls": "String"}, {"cls": "Function"}]
            nexts += [{"cls": "Value", "m": u} for u in display + [None, "km", "Euro", date_unit]]
            for nxt in nexts:
                f = Function(name)
                if nxt is not None:
                    if nxt["cls"] == "Value":
                        tok = make(1.0, nxt["m"])
                    elif nxt["cls"] == "Paren":
                        tok = Paren("(")
                    elif nxt["cls"] == "String":
                        tok = String("x")
                    else:
                        tok = Function("sqrt")
                    f.next, tok.prev = tok, f
                w.row("can", name, nxt, f.can_handle(engine, None))

        texts = names + NAMES_EXTRA
        for name in names:
            texts += [name.upper(), name.title(), " " + name, name + "x", name[:-1]]
        for _ in range(w.pick(quick=300, full=10000)):
            name = rng.choice(names)
            texts.append("".join(c.upper() if rng.random() < 0.5 else c for c in name))
            texts.append(vecutil.rand_text(rng, 6, pool=list("abcdefgilnorqstuvLOGSIN012\u0130") +
                                           list(vecutil.TEXT_POOL[-20:])))
        for text in texts:
            f = Function.as_function(text)
            w.row("as_function", text, None if f is None else f.value)


if __name__ == "__main__":
    main()
