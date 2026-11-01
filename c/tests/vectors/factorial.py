"""Vectors for tests/engine/test_factorial.c: calc/factorial.py (Factorial, Superscript).

Rows (columns after the case name):
    fact         prev, handle() of [prev, Factorial("!")]: [from_ins, to_ins, result] or the
                 exception
    fact_can     prev or null, Factorial("!").can_handle(engine, None) with prev linked before
                 it (prev may also be {"cls": "Factorial"} / {"cls": "String"})
    as_factorial text, Factorial.as_factorial(text) is not None
    sup          prev, power, handle() of [prev, Superscript(power)]: [from_ins, to_ins,
                 result] or the exception
    sup_can      prev or null, after_function, before, Superscript(2).can_handle(engine, None)
                 with prev linked before it and, when before is "Function" or "Value", a
                 Function("sin") / Value(1.0) linked before prev
    sup_str      power, Superscript(power).to_string() or the exception

prev is a Value: {"v": payload, "m": unit or null}, payload an int, a float or {"date": us,
"time": is_time}, built as Value(payload, Modifier(m, m)) ("date" as orig_value for the date
unit; in the rows captured from the test suite also "o", the orig_value). A result is {"v": payload, "m": modifier.value or null, "o": orig_value or null,
"same": the result's modifier is prev's modifier object, "s": to_string() or the exception}.

The rows start with every call of these methods the engine makes while it runs the inputs of
human_calc.py test (prev there is any token: classes other than Value, Factorial and
Superscript are written as {"cls": "String"}; before is "Value" for any non-Function).
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
    from calc.factorial import Factorial, Superscript
    from calc.function import Function
    from calc.modifier import Modifier, _lookup, _data
    from calc.value import Value
    from calc.date_value import DateValue
    from calc.string import String
    from calc.token import Token

    engine = vecutil.test_engine()
    units = sorted(set(_lookup.values()))
    display = sorted(u for u in units if _data[u.lower()][0] in ("base", "english"))
    date_unit = Token.UNPRINTABLE + "date"

    def payload(v):
        if isinstance(v, DateValue):
            return {"date": (v.value - EPOCH0) // US, "time": v.is_time}
        return v

    def make(v, unit):
        if unit is None:
            return Value(v)
        return Value(v, Modifier(unit, unit if unit != date_unit else "date"))

    def desc(value, orig=False):
        ret = {"v": vecutil.Raw(vecutil.enc(payload(value.value))),
               "m": None if value.modifier is None else value.modifier.value}
        if orig and value.modifier is not None:
            ret["o"] = value.modifier.orig_value
        return ret

    def result_desc(ret, prev):
        if isinstance(ret, vecutil.Exc):
            return ret
        from_ins, to_ins, value = ret
        mod = value.modifier
        return [from_ins, to_ins,
                {"v": vecutil.Raw(vecutil.enc(payload(value.value))),
                 "m": None if mod is None else mod.value,
                 "o": None if mod is None else mod.orig_value,
                 "same": mod is not None and mod is prev.modifier,
                 "s": vecutil.result(value.to_string)}]

    def run_handle(prev, tok):
        prev.next, tok.prev = tok, prev
        return vecutil.result(tok.handle, engine)

    # What the engine itself does while it runs the test suite (deduplicated)
    suite = {}
    orig = {"fh": Factorial.handle, "fc": Factorial.can_handle, "sh": Superscript.handle,
            "sc": Superscript.can_handle, "fa": Factorial.as_factorial}

    def note(*row):
        suite.setdefault(vecutil.dumps(vecutil.enc(list(row))), row)

    def prev_desc(tok):
        prev = tok.prev
        if prev is None:
            return None
        if isinstance(prev, Value):
            return {"cls": "Value", "m": None if prev.modifier is None else prev.modifier.value}
        if isinstance(prev, (Factorial, Superscript)):
            return {"cls": type(prev).__name__}
        return {"cls": "String"}

    def fact_handle(self, engine_):
        prev = self.prev
        arg = desc(prev, orig=True)
        try:
            ret = orig["fh"](self, engine_)
        except Exception as e:
            note("fact", arg, vecutil.Exc(vecutil.exc_name(e)))
            raise
        note("fact", arg, result_desc(ret, prev))
        return ret

    def sup_handle(self, engine_):
        prev = self.prev
        arg = desc(prev, orig=True)
        try:
            ret = orig["sh"](self, engine_)
        except Exception as e:
            note("sup", arg, self.value, vecutil.Exc(vecutil.exc_name(e)))
            raise
        note("sup", arg, self.value, result_desc(ret, prev))
        return ret

    def fact_can(self, engine_, other):
        ret = orig["fc"](self, engine_, other)
        note("fact_can", prev_desc(self), ret)
        return ret

    def sup_can(self, engine_, other):
        ret = orig["sc"](self, engine_, other)
        before = None
        if self.prev is not None and self.prev.prev is not None:
            before = "Function" if isinstance(self.prev.prev, Function) else "Value"
        note("sup_can", prev_desc(self), getattr(self, "after_function", False), before, ret)
        return ret

    def as_factorial(value):
        ret = orig["fa"](value)
        note("as_factorial", value, ret is not None)
        return ret

    Factorial.handle, Factorial.can_handle = fact_handle, fact_can
    Superscript.handle, Superscript.can_handle = sup_handle, sup_can
    Factorial.as_factorial = staticmethod(as_factorial)
    try:
        suite_engine = vecutil.test_engine()
        for line in suite_inputs():
            suite_engine.calc(line)
    finally:
        Factorial.handle, Factorial.can_handle = orig["fh"], orig["fc"]
        Superscript.handle, Superscript.can_handle = orig["sh"], orig["sc"]
        Factorial.as_factorial = staticmethod(orig["fa"])

    with vecutil.Writer("factorial") as w:
        rng = w.rng
        for row in suite.values():
            w.row(*row)

        def rand_date():
            us = rng.randint(0, MAX_US)
            if rng.random() < 0.5:
                us -= us % (86400 * 10 ** 6)
            return DateValue(us_dt(us), rng.random() < 0.3)

        def rand_unit():
            r = rng.random()
            if r < 0.5:
                return None
            if r < 0.85:
                return rng.choice(display)
            return rng.choice(units)

        # ---- Factorial
        facts = list(range(-5, 176)) + [float(x) for x in range(-3, 175)]
        for n in (0, 1, 2, 3, 5, 10, 20, 100, 169, 170, 171):
            for d in (1e-16, 4e-16, 8e-16, 1e-15, 1e-12, 1e-9, 9.9e-7, 1e-6, 1.1e-6, 1e-5, 0.4,
                      0.5, 0.6):
                facts += [n + d, n - d, -(n + d)]
        # the 1e-6 cap of the tolerance: above about 1.1e9 it is smaller than 4 * eps * value
        for n in (1200000000, 1500000000, 3000000000, 7 * 10 ** 9, 10 ** 12):
            u = math.ulp(float(n))
            for k in (1, 2, 3, 4, 8, 16, 64, 1024, 4096):
                facts += [n + k * u, n - k * u, -(n + k * u)]
            for d in (1e-7, 5e-7, 9e-7, 1e-6, 1.1e-6, 2e-6):
                facts += [n + d, n - d]
        facts += [0.3 / 0.1, 2.9999999999999996, 0.1 * 3, -0.0, 0.0, 2.5, -2.5, 170.5, 1e300,
                  -1e300, DBL_MAX, 5e-324, 2 ** 64, -(2 ** 64), int(DBL_MAX), 10 ** 30,
                  int(DBL_MAX) + 1, 2 ** 1024, -(2 ** 1100),
                  float("inf"), float("-inf"), float("nan"), 2 ** 53, 2.0 ** 53, 2.0 ** 60,
                  4503599627370495.5]
        for x in facts:
            prev = make(x, None)
            w.row("fact", desc(prev), result_desc(run_handle(prev, Factorial("!")), prev))
        for _ in range(w.pick(quick=800, full=30000)):
            r = rng.random()
            if r < 0.1:
                prev = make(rand_date(), date_unit)
            else:
                x = rng.choice(facts) if r < 0.6 else rng.randint(-10, 200)
                if rng.random() < 0.3 and not isinstance(x, int):
                    x = x * rng.choice((1, 1 + 1e-15, 1 - 1e-15, 1 + 1e-7))
                prev = make(x, rand_unit())
            w.row("fact", desc(prev), result_desc(run_handle(prev, Factorial("!")), prev))

        prevs = [None, {"cls": "Factorial"}, {"cls": "String"}]
        prevs += [{"cls": "Value", "m": u} for u in display + [None, "km", "Euro", date_unit]]
        prevs += [{"cls": "Value", "m": rng.choice(units)} for _ in range(10)]

        def build_prev(p):
            if p is None:
                return None
            if p["cls"] == "Factorial":
                return Factorial("!")
            if p["cls"] == "String":
                return String("x")
            if p["m"] == date_unit:
                return make(rand_date(), date_unit)
            return make(1.0, p["m"])

        for p in prevs:
            tok = Factorial("!")
            prev = build_prev(p)
            if prev is not None:
                prev.next, tok.prev = tok, prev
            w.row("fact_can", p, tok.can_handle(engine, None))

        for text in ("!", "!!", "", " !", "! ", "\uff01", "\u01c3", "x", "factorial"):
            w.row("as_factorial", text, Factorial.as_factorial(text) is not None)

        # ---- Superscript
        bases = [0, 1, -1, 2, -2, 3, 10, 16, -16, 255, 2 ** 32, 2 ** 53 + 1, 2 ** 64 - 1, 2 ** 64,
                 2 ** 100, -(2 ** 100), 2 ** 512, int(DBL_MAX), -int(DBL_MAX), 0.0, -0.0, 1.0,
                 -1.0, 2.0, -2.0, 0.5, -0.5, 1.5, -8.0, 10.0, 1e-300, 1e300, 5e-324, DBL_MAX,
                 1.0000000000000002, 0.9999999999999999, float("inf"), float("nan")]
        powers = [0, 1, -1, 2, -2, 3, 4, 7, 10, 20, 63, 64, 100, 255, 256, 257, 511, 512, 513,
                  1023, 1024, 1025, -1074, -1075, -1024, 10 ** 19, 10 ** 20 - 1, -(10 ** 20 - 1),
                  2 * 10 ** 20 - 2, 2 ** 63, -(2 ** 63), 2 ** 64, 5 * 10 ** 23, -(5 * 10 ** 23)]

        for base in bases:
            for power in powers:
                prev = make(base, None)
                w.row("sup", desc(prev), power,
                      result_desc(run_handle(prev, Superscript(power)), prev))
        for _ in range(w.pick(quick=1500, full=50000)):
            r = rng.random()
            if r < 0.08:
                prev = make(rand_date(), date_unit)
            elif r < 0.5:
                prev = make(rng.choice(bases), rand_unit())
            elif r < 0.75:
                prev = make(rng.randint(-2 ** rng.randint(1, 70), 2 ** rng.randint(1, 70)),
                            rand_unit())
            else:
                prev = make(rng.choice((-1, 1)) * 10.0 ** rng.uniform(-30, 30), rand_unit())
            power = rng.choice(powers) if rng.random() < 0.3 else rng.randint(-60, 1100)
            if isinstance(prev.value, int) and abs(prev.value) > 1 and power > 2000:
                power = rng.randint(-60, 1100)
            w.row("sup", desc(prev), power, result_desc(run_handle(prev, Superscript(power)), prev))

        for p in prevs + [{"cls": "Superscript"}]:
            for after_function in (False, True):
                for before in (None, "Function", "Value"):
                    tok = Superscript(2)
                    if after_function:
                        tok.after_function = True
                    prev = Superscript(3) if p is not None and p["cls"] == "Superscript" else build_prev(p)
                    if prev is not None:
                        prev.next, tok.prev = tok, prev
                        if before == "Function":
                            b = Function("sin")
                        elif before == "Value":
                            b = Value(1.0)
                        else:
                            b = None
                        if b is not None:
                            b.next, prev.prev = prev, b
                    w.row("sup_can", p, after_function, before, tok.can_handle(engine, None))

        strs = powers + [10 ** 4299, 10 ** 4300 - 1, 10 ** 4300, -(10 ** 4299), 2 ** 20000,
                         rng.randint(0, 10 ** 50)]
        for power in strs:
            w.row("sup_str", power, vecutil.result(Superscript(power).to_string))


if __name__ == "__main__":
    main()
