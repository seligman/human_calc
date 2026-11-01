"""Vectors for tests/engine/test_calc.c: Calc._normalize_unicode, Calc._mixed and the token
lists of Calc._parse_string (calc/__init__.py). Engine-module scaffolding (DESIGN.md 13.3).

Rows (columns after the case name):
    norm   value, Calc._normalize_unicode(value) (or the exception)
    mixed  whole (str or null), numerator, denominator, Calc._mixed(...) (or the exception)
    parse  value, has_last ("last" in engine.state), the token list _parse_string returns
           (null for None, or the exception)

A token is [class, payload, spaced, to_string, aux]:
    class      type(token).__name__
    payload    Value: the number (vecutil.enc), ["date", microseconds since 0001-01-01,
               is_time] for a DateValue, or "complex"; Superscript: the int; any other class:
               token.value (a str)
    spaced     getattr(token, "spaced", True)
    to_string  token.to_string() (or the exception)
    aux        Value: [modifier.value, modifier.orig_value] or null; Modifier: orig_value;
               Variable: psuedo (str or null); Paren: done; Superscript: after_function;
               any other class: null

    calc   setup, steps: one engine (the test clock and rates) runs the steps in order
    rates  the text of test_currency.json (the rates of every calc row's engine)

A calc setup is {"vars": {name: number}, "last": number or null, "sum": number or null,
"count": int or null}: values put into the engine directly before the first step (states the
engine itself never makes, like a count without a sum). A step is [input, debug, result,
escaped, root, lines, state]:
    debug      debug_mode for this step
    result     calc(input).list_to_string(), or null for None (or when an exception escaped)
    escaped    "" or the class that escaped calc() or list_to_string() (debug mode re-raises)
    root       "" or the class of the first exception that reached one of the catch sites
               (run(), Paren.handle, _add_to_total: the first sys.monitoring
               EXCEPTION_HANDLED event there, DESIGN.md 8.1)
    lines      every _dump_debug line, without the newline
    state      after the step: {"last": V, "sum": V, "count": int or null, "vars": {name: V}},
               V = [payload, modifier.value, modifier.orig_value, exact_days, span] or null
Calc sources: test_cases.txt and README.md in order on one engine, once in normal and once in
debug mode; probe sequences (the cases the mutation check of calc.c needed); probes on crafted
states; and random token soup sequences (both modes).

Sources: every call the reference engine makes while it runs test_cases.txt, README.md and
tests/corpus/unicode.jsonl (one engine, lines in order, as human_calc.py test does); the lines of
tests/corpus/long.jsonl (quick tier: _parse_string called directly on a fresh engine; full tier:
through calc() on one engine); probe inputs; and random inputs: strings over the code points
_normalize_unicode rewrites, _mixed arguments (commas, leading zeros, 30-digit ints, zero
denominators, long wholes), and token soup for _parse_string with and without "last" in the
state. Every _parse_string row is a direct call on the engine named in the row (the state only
matters through has_last; the clock is the test clock).
"""

import vecutil

import contextlib
import io
import os
import sys
from datetime import datetime, timedelta

EPOCH0 = datetime(1, 1, 1)
US = timedelta(microseconds=1)

NORM_PROBES = [
    "\uff21\u0301", "\uff11\uff0b\uff12", "\uffe55 + \uffe13", "a\u00a0\u2000b\u3000c",
    "a\t\n\r\x0b\x0c\x1c\x1d\x1e\x1f\x85b", "a\u200bb", "x\u180ey", "1e\u22125", "1.e\u22125",
    "e\u22125", "1E\u27965", "1e\u2212x", "2000\u22128\u22124", "twenty\u2212one",
    "a\u1680b", "a\u2010b\u2011c\u2012d\u2013e\u2014f\ufe58g\ufe63h", "1\u27952\ufe623",
    "log\u2081\u2080 100", "LOG\u2081\u2080 5", "log\u2082 8", "sin\u207b\u00b9 0.5",
    "SIN\u207b\u00b9 1", "\u017fin\u207b\u00b9 1", "S\u0130N\u207b\u00b9 0.5", "Cos\u207b\u00b9",
    "tan\u207b\u00b9\u207b\u00b9", "30\u00ba", "30\u02da", "30\u1d52", "\u00b3\u2044\u2084",
    "1 1\u20442", "2\u00b9\u2044\u2082", "1 \u00b9\u2044\u2082", "\u00b9\u00b2\u2044\u2083",
    "ab1 1\u20442", "k1\u20442", "K1\u20442", "\u01301\u20442", "\u0131 1\u20442", "x\u20442",
    "1\u20442\u20443", "\u00bd\u2044\u2084", "007\u20440", "1,000 3\u20440", "1,2 3\u20444",
    "1, 1\u20442", "100\u20441", "15\u204410", "0\u20445", "3 0\u20441", "1\u204430",
    "1\u20441000000", "1\u204410000000", "1\u20443000000",
    "10000000000000000000000000000\u20441", "123456789012345678901234567890 1\u20443",
    "3\u20444\u2082", "1" * 15 + "\u2044" + "2" * 15, "1" * 16 + "\u2044" + "2" * 15,
    "1\u00bd", "1\u2153", "1\u2154", "\u2153\u2154", "1.5\u00bd", "x\u00bd", "1,000\u00bd",
    "\u00bd5", "1 \u00bd", "1  \u00bd", "a1\u00bd", "\u01301\u00bd", "1\u00bd\u00bd",
    "1,\u00bd", "2\u00b74", "2\u00d74", "3\u03874", "8\u00f72", "8\u22152", "8\u27972",
    "[1+2]*{3}", "\u00b2\u221a9", "\u00b3\u221a8", "\u2074\u221a16", "\u221a9", "\u221b8",
    "\u221c16", "x\u00b2\u221a9", "1\u22602", "1\u22642", "1\u22652", "1\u22662", "1\u2a7d2",
    "1\u22672", "1\u2a7e2", "\u03c0", "2\u03c0r", "\u03c0\u03c0", "\u03c0\u03bf\u03c3\u03cc",
    "\u1f00\u03c0", "\u03c0\u1fff", "\u201chi\u201d", "\u2018hi\u2019", "\u212a", "\u2126",
    "\u2000", "\u037e", "\uf900", "\u0958", "\ufb2c", "\U0001d160", "\u2160\u2163",
    "A\u030a", "\u00e9", "e\u0301", "\uff5e\uff01\uff00\uff5f", "\ue000\ue001\ue011",
    "9" * 9990 + "\u2153", "1," * 4000 + "1\u00bd",
    "7" * 9000 + " " + "1" * 15 + "\u2044" + "3" * 15,
]

MIXED_PROBES = [
    (None, 1, 2), (None, 3, 4), (None, 100, 1), (None, 15, 10), (None, 0, 5), ("3", 0, 1),
    ("1", 1, 3), ("1", 2, 3), (None, 1, 30), (None, 1, 1000000), (None, 1, 10000000),
    (None, 1, 3000000), (None, 10 ** 28, 1), ("123456789012345678901234567890", 1, 3),
    (None, 3, 42), ("007", 7, 0), ("1,000", 3, 0), (None, 0, 0), ("1,2", 3, 4), ("1,", 1, 2),
    ("0", 1, 2), ("000", 0, 1), ("1,000", 1, 2), ("9" * 40, 1, 3), ("9" * 40, 2, 3),
    ("1" + "0" * 400, 1, 7), (None, 10 ** 30 - 1, 7), (None, 7, 10 ** 30 - 1), (None, 1, 8),
    ("5", 10 ** 29, 3), (None, 10 ** 29, 10 ** 29), ("0,0,0", 1, 9), (None, 5, 1),
    ("1", 99999999999999999999, 100000000000000000000),
]

PARSE_PROBES = [
    "5!=3", "1 === 2", "a<<=3", "5 ! = 3", "!=", "<= 5", "5 >= = 3", "5!!", "!==", "<>=",
    "5#in", "a?b", "30\u00b0", "5\ue0056", "1..5", "...", ",", "\u0663", "x\u0663 = 5",
    "\u2460 + 1", "Total cost: 5", "x: 5", "My_var: 3", "e-mail: 5", "a1: 5", "#note: 5",
    "Start time: 12:30", "x\u00b2y: 4", "x\u0663y: 4", "x\u0301: 5", "a\u203fb: 5",
    "\u20acfood stuff: 5", "Lunch stuff: * 3", "Lunch stuff: + 3", "+5", "-5", "- five hundred",
    "-last", "- avg", "- 2 km", "- 3 4", "% 5", "* 3", "in km", "to km", "- 5 6", "-", "+",
    "- x", "- sum", "-(5)", "2.5e3km", "1e5 km", "5 in", "5in", "5 in km", "0x10", "0b101",
    "0o17", "1e400", "1" * 400, "0x" + "f" * 300, "0xfffffffffffff8" + "0" * 242,
    "0xfffffffffffff8" + "0" * 241 + "1", "1,000,000", "1,000e3", "(" * 100 + "1" + ")" * 100,
    "(" * 101 + "1" + ")" * 101, "))))((1", ")" + "(" * 101 + "1" + ")" * 101, "2.5e3in",
    "1e5in", "1e5 in", "0x10in", "2021-01-01\ue011", "\ue010\ue011", "5 \ue000 6",
    "South Korean Won", "5 us dollars", "5 \u017fwiss franc", "S\u0130LVER, troy ounce",
    "south korean won_x", "5 fluid ounces", "x = 5", "x == 5", "x := 5", "a b c", "Pi\u00f1a",
    "\u03a3\u03b1 = 5", "\u0391\u03a3 = 5", "\u00e9t\u00e9 + 1", "sqrt 16 + 9", "sin\u00b230",
    "2\u00b3", "2\u00b3\u207b\u00b9", "x\u00b2", "log10(100)", "log2 8", "twenty-one",
    "one hundred and five", "a hundred", "$5", "$$5", "\u20ac5", "\u00a35 + \u00a55", "5$",
    "5 %", "50% of 20", "5 mod 3", "5 xor 3", "5 & 3", "5 | 3", "5 << 2", "5 >> 1", "2^3",
    "2 ** 3", "10 / 2", "10 per 2", "now", "today", "pi * e", "jan 5 2021", "1:30 pm",
    "12:30", "2021-06-01 - 2021-01-01", "5 km in mi", "5 km as mi", "last + 1", "sum", "avg",
    "average", "x\u0307 = 1", "\u0130 = 5", "\U0001f600 5", "5 \ufffd 6", "\x00 5", "",
    " ", "\u3000", "5 km/h", "5 kph", "100 km/h in mph", "5 sq ft", "5 ft\u00b2", "5 m^2",
]

ATOMS = [
    "5", "1,000", "2.5", ".5", "5.", "2.5e3", "1e5", "0x1f", "0b101", "0o7", "2021-01-01",
    "12:30", "1:30 pm", "jan 5", "km", "mi", "$", "\u20ac", "\u00a3", "\u00a5", "eur", "usd",
    "in", "to", "as", "per", "of", "+", "-", "*", "/", "^", "%", "&", "|", "<<", ">>", "!",
    "!=", "<=", ">=", "==", "=", "<", ">", ":", "(", ")", "five", "hundred", "thousand",
    "twenty-one", "a", "and", "x", "y", "last", "avg", "sum", "pi", "e", "now", "today",
    "sqrt", "sin", "abs", "log10", "Lunch stuff:", "x:", "\u00e9", "\u00b2", "\u2153", "\u221a",
    "\u2212", "\u00d7", "\u03c0", "\u00b0", "\ue000", "\ue011", "\u3000", "\u00a0", "...", ".",
    ",", "#", "?", "\"", "'", "@", "mod", "xor", "or", "AND", "Km", "KM", "hours", "days",
    "1\u20442", "\u00bd", "\u0663", "\u0301", "S\u0130", "\u017f", "\u212a", "3\u00b9",
]

NORM_POOL = (
    [chr(c) for c in range(0x20, 0x7F)]
    + list("\uff01\uff10\uff11\uff21\uff41\uff5e\uff00\uff5f\uffe5\uffe1\u00a0\u2000\u2005\u200a"
           "\u202f\u205f\u3000\t\n\x1c\x85\u2028\u1680\u180e\u200b\u2212\u2796\u2010\u2011\u2012"
           "\u2013\u2014\ufe58\ufe63\u2795\ufe62\u2081\u2080\u2082\u207b\u00b9\u00ba\u02da\u1d52"
           "\u2070\u00b2\u00b3\u2074\u2079\u2083\u2089\u2044\u00bd\u2153\u2154\u00bc\u00be\u2152"
           "\u215f\u2189\u00b7\u00d7\u2217\u2219\u22c5\u2715\u2716\u2a2f\u00f7\u2215\u2797\u221a"
           "\u221b\u221c\u2260\u2264\u2265\u2266\u2a7d\u2267\u2a7e\u03c0\u03b1\u1f00\u0370\u03ff"
           "\u201c\u201d\u2018\u2019\u0130\u0131\u017f\u212a\u0301\u0387\u037e\u2126\ufb2c\u00e9"
           "\U0001d160\ue000\ue011")
    + list("0123456789") * 6 + list("sincotalgSINCOTALGeE.,") * 2 + ["log", "sin", "cos", "tan"]
)


def us(dt):
    return (dt - EPOCH0) // US


def main():
    calc = vecutil.import_calc()
    from calc.date_value import DateValue
    from calc.factorial import Superscript
    from calc.modifier import Modifier
    from calc.paren import Paren
    from calc.value import Value
    from calc.variable import Variable

    sys.path.insert(0, os.path.join(vecutil.ROOT, "c", "tests", "corpus"))
    import corpus

    Calc = calc.Calc

    def payload(t):
        if isinstance(t, Value):
            v = t.value
            if isinstance(v, DateValue):
                return ["date", us(v.value), v.is_time]
            if isinstance(v, complex):
                return "complex"
            return v
        if isinstance(t, Superscript):
            return t.value
        return t.value

    def aux(t):
        if isinstance(t, Value):
            m = t.modifier
            return None if m is None else [m.value, m.orig_value]
        if isinstance(t, Modifier):
            return t.orig_value
        if isinstance(t, Variable):
            return t.psuedo
        if isinstance(t, Paren):
            return t.done
        if isinstance(t, Superscript):
            return getattr(t, "after_function", False)
        return None

    def dump(head):
        if head is None:
            return None
        out = []
        cur = head
        while cur is not None:
            out.append([type(cur).__name__, payload(cur), getattr(cur, "spaced", True),
                        vecutil.result(cur.to_string), aux(cur)])
            cur = cur.next
        return out

    with vecutil.Writer("engine_calc") as w:
        rng = w.rng
        seen = set()

        def emit(case, *cols):
            key = vecutil.dumps(vecutil.enc([case] + list(cols)))
            if key not in seen:
                seen.add(key)
                w.row(case, *cols)

        orig_norm = Calc._normalize_unicode
        orig_mixed = Calc._mixed
        orig_parse = Calc._parse_string

        def norm(self, value):
            out = vecutil.result(orig_norm, self, value)
            emit("norm", value, out)
            if isinstance(out, vecutil.Exc):
                return orig_norm(self, value)
            return out

        def mixed(whole, numerator, denominator):
            out = vecutil.result(orig_mixed, whole, numerator, denominator)
            emit("mixed", whole, numerator, denominator, out)
            if isinstance(out, vecutil.Exc):
                return orig_mixed(whole, numerator, denominator)
            return out

        def parse(self, value):
            has_last = "last" in self.state
            try:
                head = orig_parse(self, value)
            except Exception as e:  # noqa: BLE001 (the class is the result)
                emit("parse", value, has_last, vecutil.Exc(vecutil.exc_name(e)))
                raise
            emit("parse", value, has_last, dump(head))
            return head

        def direct(engine, value, has_last):
            if has_last:
                engine.state["last"] = Value(0.0)
            else:
                engine.state.pop("last", None)
            try:
                engine._parse_string(value)
            except Exception:  # noqa: BLE001 (recorded by the wrapper)
                pass

        Calc._normalize_unicode = norm
        Calc._mixed = staticmethod(mixed)
        Calc._parse_string = parse
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                engine = vecutil.test_engine()
                lines = [text for _, _, text in corpus.test_inputs(vecutil.ROOT)]
                long_lines = []
                for path in corpus.corpus_files():
                    base = os.path.basename(path)
                    if base.startswith("unicode"):
                        lines += corpus.read_lines(path)
                    elif base.startswith("long"):
                        long_lines += corpus.read_lines(path)
                if w.full:
                    lines += long_lines
                for text in lines:
                    try:
                        engine.calc(text)
                    except Exception:  # noqa: BLE001 (recorded by the wrapper)
                        pass
                fresh = vecutil.test_engine()
                if not w.full:
                    for text in long_lines:
                        direct(fresh, text, False)

                for text in NORM_PROBES:
                    vecutil.result(fresh._normalize_unicode, text)
                for args in MIXED_PROBES:
                    vecutil.result(Calc._mixed, *args)
                for text in PARSE_PROBES + NORM_PROBES:
                    direct(fresh, text, False)
                    direct(fresh, text, True)

                for _ in range(w.pick(quick=1500, full=40000)):
                    n = rng.randint(1, w.pick(quick=12, full=24))
                    vecutil.result(fresh._normalize_unicode,
                                   "".join(rng.choice(NORM_POOL) for _ in range(n)))

                def rand_whole():
                    if rng.random() < 0.3:
                        return None
                    n = rng.choice((1, 1, 2, 3, 5, 10, 20, 30, 45, 80, 300))
                    s = "".join(rng.choice("0123456789,,") for _ in range(n))
                    return rng.choice("123456789") + s

                def rand_int():
                    r = rng.random()
                    if r < 0.15:
                        return 0
                    if r < 0.6:
                        return rng.randint(1, 20)
                    return rng.randint(1, 10 ** rng.randint(1, 30))

                for _ in range(w.pick(quick=600, full=20000)):
                    vecutil.result(Calc._mixed, rand_whole(), rand_int(), rand_int())

                for _ in range(w.pick(quick=1500, full=40000)):
                    n = rng.randint(1, w.pick(quick=8, full=14))
                    parts = []
                    for _ in range(n):
                        parts.append(rng.choice(ATOMS))
                        parts.append(rng.choice(("", " ", " ", " ", "  ", "\u3000")))
                    direct(fresh, "".join(parts).strip(), rng.random() < 0.5)

                mutate_from = [text for _, _, text in corpus.test_inputs(vecutil.ROOT)]
                for _ in range(w.pick(quick=500, full=20000)):
                    text = corpus.mutate(rng, rng.choice(mutate_from))
                    direct(fresh, text, rng.random() < 0.5)
        finally:
            Calc._normalize_unicode = orig_norm
            Calc._mixed = orig_mixed
            Calc._parse_string = orig_parse

        calc_rows(w, Calc, payload, corpus)


# Sequences joined by " ;; " (each line its own calc() call)
CALC_PROBES = [
    "2\u00b2 (3)", "50% + 3!", "3! 50%", "1 | 2 3 xor 4", "1 & 2 3 xor 4", "1 << 2 3 & 4",
    "1 xor 2 3 | 4", "-5 - -3", "- 5 - - 3", "((1)) (2)", "((1) + (2)) pi", "((2) (3))",
    "(1 + (2) + (3))", "x = 4 ;; ((x) + (1))", "today (12:00 + 1 day)", "today 12:00",
    "12:00 today", "5 $ (2)", "5 $ pi", "2 $ sqrt 4", "$1 ;; 1 rub ;; sum",
    "0xffffffffffffffff ;; 1.0 ;; sum", "0x10 ;; 5 km ;; sum",
    "5 days ;; 2021-07-01 - 2021-06-01 ;; sum", "5 km in m x = 3", "x = 5 km in m",
    "a = b = 5 ;; a + b", "sin\u00b2(30)", "(sin\u00b230)", "sin\u00b230", "2 pi", "2(3)",
    "(2)(3)", "(2)pi", "(3)2", "2 sqrt 2", "(1) thousand", "(2) five", "x = 3 ;; 2 x",
    "x = 3 ;; five x", "(((1 + 2) * 3)", "1 + 2) * 3", "sqrt(-1)", "(1/0)", "1/0 + (2)",
    "5 km in mi", "1 hour and 30 minutes", "0x1 << 0x3ff ;; 0x1 << 0x3ff ;; 1.5 ;; sum",
    "3e-323 ;; 0 - 2.5e-323 ;; sum", "$5 ;; 3 eur ;; sum ;; average", "5 ;; reset ;; sum",
    "1e308 ;; 1e308 ;; sum", "5 km ;; 3 kg ;; sum", "10 ;; 0x10 ;; sum",
    "2021-01-01 ;; 5 ;; sum", "x = 5 ;; x ;; sum", "now ;; 5 ;; last + 1", "",
    "#", "show", "x = 2 ;; show", "1 + 2 + 3 ;; + 10", "- 5 ;; -", "5 in", "in 5",
    "sin\u00b2(30) five", "sin\u00b2(30) thousand", "sin\u00b2(30) a hundred",
    "sqrt\u00b2 16 km", "sqrt\u00b2 16 five", "x = 2 ;; sin\u00b2(30) x",
]

# (setup, sequence): states the engine never makes itself, and names it never assigns
CALC_STATE_PROBES = [
    ({"vars": {"last": 7.0}, "last": 5.0}, "2 last"),
    ({"vars": {"sum": 7.0}, "sum": 5.0, "count": 1}, "2 sum"),
    ({"vars": {"thousand": 7.0}}, "2 thousand"),
    ({"vars": {"thousand": 7.0}}, "(1) thousand"),
    ({"vars": {"five": 7.0}}, "(2) five ;; 3 five"),
    ({"vars": {"a": 2}}, "a hundred ;; 3 a ;; (3) a"),
    ({"count": 3}, "sum ;; average ;; 5 ;; sum"),
    ({"sum": 5, "count": 0}, "3 ;; sum"),
    ({"sum": 5.5}, "1 ;; sum"),
    ({"count": 2, "sum": 0x10}, "1.5 ;; sum ;; average"),
]


def calc_rows(w, Calc, payload, corpus):
    from calc.date_value import DateValue
    from calc.value import Value

    rng = w.rng
    catch_sites = {"Calc._calc_nodes.<locals>.run", "Paren.handle", "Calc._add_to_total"}
    calc_dir = os.path.dirname(os.path.abspath(sys.modules["calc"].__file__))
    mon = sys.monitoring
    tool = None
    for t in (4, 3, 5):
        try:
            mon.use_tool_id(t, "engine_calc")
            tool = t
            break
        except ValueError:
            continue
    if tool is None:
        raise RuntimeError("no free sys.monitoring tool id")
    watch = {"active": False, "root": None}

    def on_handled(code, offset, exc):
        if watch["active"] and watch["root"] is None and code.co_qualname in catch_sites and \
                os.path.dirname(os.path.abspath(code.co_filename)) == calc_dir:
            watch["root"] = exc

    mon.register_callback(tool, mon.events.EXCEPTION_HANDLED, on_handled)
    mon.set_events(tool, mon.events.EXCEPTION_HANDLED)

    lines_out = []
    orig_dump = Calc._dump_debug

    def dump_debug(self, cur):
        if self.debug_mode:
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                orig_dump(self, cur)
            text = buf.getvalue()
            if not text.endswith("\n"):
                raise RuntimeError("unexpected _dump_debug output")
            lines_out.append(text[:-1])

    def val(v):
        if v is None:
            return None
        if not isinstance(v, Value):
            raise RuntimeError(f"state holds a {type(v).__name__}")
        m = v.modifier
        return [payload(v), None if m is None else m.value, None if m is None else m.orig_value,
                v.exact_days, None if v.span is None else list(v.span)]

    def state_of(engine):
        return {"last": val(engine.state.get("last")), "sum": val(engine.state.get("sum")),
                "count": engine.state.get("count"),
                "vars": {k: val(engine.variables[k]) for k in sorted(engine.variables)}}

    def clean(text):
        return "".join("\ufffd" if 0xD800 <= ord(ch) <= 0xDFFF else ch for ch in text)

    def run(setup, inputs, debug):
        engine = vecutil.test_engine()
        for name, x in setup.get("vars", {}).items():
            engine.variables[name] = Value(x)
        for key in ("last", "sum"):
            if setup.get(key) is not None:
                engine.state[key] = Value(setup[key])
        if setup.get("count") is not None:
            engine.state["count"] = setup["count"]
        engine.debug_mode = debug
        steps = []
        for text in inputs:
            text = clean(text)
            del lines_out[:]
            result, escaped = None, ""
            watch["root"] = None
            watch["active"] = True
            try:
                r = engine.calc(text)
                result = None if r is None else r.list_to_string()
            except Exception as e:  # noqa: BLE001 (the class is the result)
                escaped = vecutil.exc_name(e)
            finally:
                watch["active"] = False
            root = "" if watch["root"] is None else vecutil.exc_name(watch["root"])
            steps.append([text, debug, result, escaped, root, list(lines_out), state_of(engine)])
        w.row("calc", setup, steps)

    Calc._dump_debug = dump_debug
    try:
        with open(os.path.join(vecutil.ROOT, "test_currency.json"), encoding="utf-8") as f:
            w.row("rates", f.read())
        tests = [text for _, _, text in corpus.test_inputs(vecutil.ROOT)]
        run({}, tests, False)
        run({}, tests, True)
        for seq in CALC_PROBES:
            for debug in (False, True):
                run({}, seq.split(" ;; "), debug)
        for setup, seq in CALC_STATE_PROBES:
            for debug in (False, True):
                run(setup, seq.split(" ;; "), debug)
        for _ in range(w.pick(quick=300, full=6000)):
            seq = []
            for _ in range(rng.randint(1, 4)):
                n = rng.randint(1, w.pick(quick=8, full=14))
                parts = []
                for _ in range(n):
                    parts.append(rng.choice(ATOMS))
                    parts.append(rng.choice(("", " ", " ", " ", "  ")))
                seq.append("".join(parts).strip())
            run({}, seq, rng.random() < 0.5)
        for _ in range(w.pick(quick=200, full=4000)):
            seq = [corpus.mutate(rng, rng.choice(tests)) for _ in range(rng.randint(1, 3))]
            run({}, seq, rng.random() < 0.5)
    finally:
        Calc._dump_debug = orig_dump
        mon.set_events(tool, 0)
        mon.register_callback(tool, mon.events.EXCEPTION_HANDLED, None)
        mon.free_tool_id(tool)


if __name__ == "__main__":
    main()
