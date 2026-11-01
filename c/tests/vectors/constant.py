"""Vectors for tests/engine/test_constant.c: calc/constant.py (Constant).

Rows (columns after the case name):
    const        name, override, hours, Constant.cached[name](engine) for an engine made with
                 date_override = the datetime of override (microseconds since 0001-01-01) and
                 utc_zone_offset = hours: a result or the exception
    as_constant  text, Constant.as_constant(text).value, or null for None
    names        [the keys of Constant.cached, in order]

The as_constant rows start with every token text the tokenizer tries while the engine runs
the inputs of human_calc.py test.

A result is {"v": payload, "m": modifier.value or null, "o": orig_value or null,
"s": to_string()}, payload a float or {"date": us, "time": is_time}.
"""

import vecutil

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
    calc = vecutil.import_calc()
    from calc.constant import Constant
    from calc.date_value import DateValue

    if Constant.cached is None:
        Constant.cache_functions()
    names = list(Constant.cached)

    def payload(v):
        if isinstance(v, DateValue):
            return {"date": dt_us(v.value), "time": v.is_time}
        return v

    def result_desc(ret):
        if isinstance(ret, vecutil.Exc):
            return ret
        mod = ret.modifier
        return {"v": vecutil.Raw(vecutil.enc(payload(ret.value))),
                "m": None if mod is None else mod.value,
                "o": None if mod is None else mod.orig_value,
                "s": vecutil.result(ret.to_string)}

    # Every as_constant the tokenizer makes while the engine runs the test suite
    suite = {}
    orig_as = Constant.as_constant

    def as_constant(value):
        ret = orig_as(value)
        suite.setdefault(value, None if ret is None else ret.value)
        return ret

    Constant.as_constant = staticmethod(as_constant)
    try:
        suite_engine = vecutil.test_engine()
        for line in suite_inputs():
            suite_engine.calc(line)
    finally:
        Constant.as_constant = staticmethod(orig_as)

    with vecutil.Writer("constant") as w:
        rng = w.rng
        w.row("names", names)
        for text, value in suite.items():
            w.row("as_constant", text, value)

        clocks = [(dt_us(datetime(2021, 7, 1, 12, 34, 56)), 5),
                  (dt_us(datetime(2021, 7, 1, 12, 34, 56)), 0),
                  (dt_us(datetime(2021, 7, 1, 12, 34, 56, 999999)), -7),
                  (dt_us(datetime(2021, 7, 1, 23, 59, 59, 999999)), 1),
                  (dt_us(datetime(2021, 7, 1, 0, 0, 0, 1)), -1),
                  (dt_us(datetime(2020, 2, 29, 21, 0)), 3),
                  (dt_us(datetime(2021, 12, 31, 22, 0)), 2),
                  (0, 0), (0, -1), (0, 1), (MAX_US, 0), (MAX_US, 1), (MAX_US, -1),
                  (MAX_US - DAY, 23), (MAX_US - DAY, 24), (dt_us(datetime(1, 1, 1, 5)), -5),
                  (dt_us(datetime(1, 1, 1, 5)), -6), (dt_us(datetime(5000, 6, 15)), 240000),
                  (dt_us(datetime(5000, 6, 15)), -240000), (dt_us(datetime(5000, 6, 15)), 10 ** 8)]
        for _ in range(w.pick(quick=60, full=3000)):
            us = rng.randint(0, MAX_US)
            if rng.random() < 0.3:
                us -= us % (10 ** 6)
            clocks.append((us, rng.choice((0, 5, -5, rng.randint(-48, 48), rng.randint(-10 ** 5, 10 ** 5)))))
        for override, hours in clocks:
            engine = calc.Calc(date_override=us_dt(override), utc_zone_offset=hours)
            for name in names:
                ret = vecutil.result(Constant.cached[name], engine)
                w.row("const", name, override, hours, result_desc(ret))

        texts = names + ["", "PI", "Pi", "E", "NOW", "Today", "uToday", "UTCNOW", "tau", "p",
                         "pie", " pi", "pi ", "\u03c0", "\u03a0", "\u0130", "TOD\u0130Y",
                         "\uff50\uff49", "utc", "unow2", "now\u0307", "\u212a"]
        for name in names:
            texts += [name.upper(), name.title(), name + "s", name[:-1]]
        for _ in range(w.pick(quick=200, full=5000)):
            name = rng.choice(names)
            texts.append("".join(c.upper() if rng.random() < 0.5 else c for c in name))
            texts.append(vecutil.rand_text(rng, 5, pool=list("eiponwtudayc\u0130\u0131EPI") +
                                           list(vecutil.TEXT_POOL[-20:])))
        for text in texts:
            c = Constant.as_constant(text)
            w.row("as_constant", text, None if c is None else c.value)


if __name__ == "__main__":
    main()
