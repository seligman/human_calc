"""Vectors for tests/rt/test_floats.c: float text, Decimal and JSON numbers (src/rt/py_float_text.c,
py_decimal.c, py_json_number.c; DESIGN.md 5.4, 5.7, 13.3).

Floats are vecutil-encoded by their bits, ints exactly; a result column may be {"exc": ...}.
A Decimal is written as a "dec" triple [negative, coefficient, exponent] (its as_tuple()).
Decimal work runs in decimal.localcontext() with prec 400, as the engine's does.

Rows (columns after the case name):
    repr      x, repr(x)
    short     x, digits, decpt, negative   (shortest round-trip digits: 0.d1..dn * 10**decpt;
              zero is "0", decpt 1)
    prec      x, p, digits, decpt, negative   (the p digits of format(x, ".<p-1>e"))
    fixed     x, places, grouping, f"{x:,.<places>f}" or f"{x:.<places>f}"
    g         x, p, format(x, ".<p>g")
    round     x, n, round(x, n)
    parse     s, float(s)   (s within the header's grammar, or malformed for Python too)
    dec       s, Decimal(s) triple, str(d), d.normalize() triple, ndigits, float(d),
              format(d, "f"), format(d, ",f")   (every column after s may be an exception
              when Decimal(s) raises)
    decg      x, p, Decimal(format(x, ".<p>g")) triple
    decrepr   x, Decimal(repr(x)) triple
    quant     s, places, d.quantize(Decimal(1).scaleb(-places), ROUND_HALF_UP) triple or
              exception, f"{q:,.<places>f}"
    fmtf      s, places, grouping, format(Decimal(s), "[,].<places>f")
    add       a, b, prec, triple of a + b in a context of precision prec, str of it
    div       a, b, prec, triple of a / b, str of it (or the exception)
    mixed     whole (str or null), numerator, denominator, Calc._mixed(...)
    fixedv    x, places, Value._fixed(x, places)   (x an int or a float)
    shownbig  x, Value._shown(x) for abs(x) >= 2**53
    english   x, s = format(x, ".7g"), f"{Decimal(s):f}"
    decimals  x, operator_._decimals(x)
    json      literal, json.loads(literal) (int, float or ValueError)
    int       s, base, int(s, base) (special_tokens.py's hex, octal, binary and decimal runs;
              U5's py_num_int_from_digits)
"""

import vecutil

import decimal
import json
import math
import re
import sys
from decimal import Decimal
from fractions import Fraction

DBL_MAX = sys.float_info.max
INF = float("inf")
NAN = float("nan")
TINY = vecutil.bits_float(1)

GRAMMAR = re.compile(r"[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?\Z")


def dec_triple(d):
    t = d.as_tuple()
    coeff = int("".join(map(str, t.digits))) if t.digits else 0
    return [bool(t.sign), coeff, t.exponent]


def finite(x):
    return not (math.isnan(x) or math.isinf(x))


def next_up(x):
    return math.nextafter(x, INF)


# ---- generators --------------------------------------------------------------------------

def short_decimal(rng, max_digits=17, max_exp=25):
    nd = rng.randint(1, max_digits)
    digits = rng.randrange(10 ** (nd - 1), 10 ** nd)
    x = float(f"{digits}e{rng.randint(-max_exp, max_exp)}")
    return -x if rng.random() < 0.3 else x


def engine_double(rng):
    """Values like the ones the engine formats and rounds."""
    r = rng.random()
    if r < 0.25:
        return short_decimal(rng, rng.choice((3, 6, 10, 17)), rng.choice((3, 8, 25)))
    if r < 0.4:
        a = short_decimal(rng, 8, 6)
        b = short_decimal(rng, 8, 6)
        return a + b if rng.random() < 0.5 else a - b
    if r < 0.5:
        a = short_decimal(rng, 6, 4)
        b = short_decimal(rng, 6, 4) or 1.0
        return a * b if rng.random() < 0.5 else a / b
    if r < 0.65:
        j = rng.randint(1, 40)
        return rng.randrange(-(1 << 45), 1 << 45) / (1 << j)
    if r < 0.75:
        return float(rng.randrange(-10 ** 17, 10 ** 17)) + rng.choice((0.0, 0.5, 0.25, -0.5))
    if r < 0.85:
        return rng.uniform(-1, 1) * 10.0 ** rng.randint(-20, 20)
    return vecutil.rand_double(rng)


def edge_doubles(full=True):
    """Edge values; the quick tier takes every fourth of the generated sweeps."""
    out = list(vecutil.edge_doubles())
    out += [DBL_MAX, -DBL_MAX, TINY, -TINY, 2.2250738585072014e-308, 2.225073858507201e-308,
            1e23, 8.41e21, 5e-324, 9007199254740993.0, 0.1 + 0.2, 1 / 3, 2 / 3, 2.675, 1.005,
            0.125, 0.375, -0.125, 1e-7, 123456789012345680.0, 1.7976931348623157e308,
            4.940656458412465e-324, 2.5e-323, 3e-323, 1e-320, 999999999999999.5, 0.5, 1.5,
            2.5, -2.5, 1e21, 1e-6, 1e-5, 1e15, 1e16, 1e17, 123.0, 1000000000000000.0,
            6.022e23, 12345678901234.56, 1e300, 9.999999999999999e22, 5e-310]
    for e in range(-1074, 1024, 31 if full else 124):
        out.append(2.0 ** e)
        out.append(next_up(2.0 ** e))
        out.append(math.nextafter(2.0 ** e, 0.0))
    for e in range(-325, 309, 3 if full else 12):
        x = float(f"1e{e}")
        out += [x, next_up(x), math.nextafter(x, 0.0), float(f"9.999999999999999e{e}")]
    return out if full else [x for i, x in enumerate(out) if i % 3 == 0 or x in EDGE_KEEP]


# Values every tier keeps.
EDGE_KEEP = {0.0, 1.0, 0.5, 1e16, 1e-5, 5e-324, DBL_MAX, -DBL_MAX, 2.675, 1.005, 0.125, 0.375,
             -0.125, 999999999999999.5, 1e23, 2.5, -2.5, 1e-7, 123.0, 6.022e23}


def rand_digits_text(rng):
    """A number text in the header's grammar with random shape."""
    r = rng.random()
    if r < 0.4:
        nd = rng.randint(1, 25)
    elif r < 0.8:
        nd = rng.randint(1, 60)
    elif r < 0.97:
        nd = rng.randint(60, 900)
    else:
        nd = rng.randint(900, 3000)
    digits = "".join(rng.choice("0123456789") for _ in range(nd))
    if rng.random() < 0.3:
        digits = "0" * rng.randint(1, 20) + digits
    if rng.random() < 0.3:
        digits = digits + "0" * rng.randint(1, 40)
    point = rng.randint(0, len(digits))
    if rng.random() < 0.7:
        text = digits[:point] + "." + digits[point:]
    else:
        text = digits
    if rng.random() < 0.6:
        e = rng.randint(-400, 400) if rng.random() < 0.8 else rng.randint(-5000, 5000)
        text += rng.choice("eE") + rng.choice(("", "+", "-") if e >= 0 else ("-",)) + str(abs(e))
    if rng.random() < 0.3:
        text = rng.choice("+-") + text
    return text


def midpoint_texts(rng, x):
    """Exact decimal texts of the midpoint between x > 0 and the next double, and just off it."""
    y = next_up(x)
    if not finite(y):
        return []
    mid = (Fraction(x) + Fraction(y)) / 2
    # mid = n / 2**k exactly: its decimal expansion terminates.
    n, d = mid.numerator, mid.denominator
    k = d.bit_length() - 1
    digits = str(n * 5 ** k)
    if k:
        digits = digits.rjust(k + 1, "0")
        text = digits[:-k] + "." + digits[-k:]
    else:
        text = digits
    out = [text, text + "0" * rng.randint(1, 50) + "1"]
    # one unit below in the last digit (still within the same pair of doubles)
    last = len(text.rstrip("0").rstrip(".")) - 1
    if text[last] != "0" and text[last] != ".":
        out.append(text[:last] + str(int(text[last]) - 1) + "9" * rng.randint(0, 30))
    return out


# ---- writers -----------------------------------------------------------------------------

def shortest(x):
    if x == 0:
        return "0", 1
    t = Decimal(repr(abs(x))).as_tuple()
    digits = "".join(map(str, t.digits))
    decpt = len(digits) + t.exponent
    digits = digits.rstrip("0") or "0"
    return digits, decpt


def prec_digits(x, p):
    s = format(abs(x), ".%de" % (p - 1))
    mant, exp = s.split("e")
    return mant.replace(".", ""), int(exp) + 1


def gen_repr(w):
    rng = w.rng
    for x in edge_doubles(w.full):
        w.row("repr", x, repr(x))
        if finite(x):
            d, p = shortest(x)
            w.row("short", x, d, p, math.copysign(1.0, x) < 0)
    for _ in range(w.pick(quick=3000, full=2000000)):
        x = vecutil.rand_double(rng)
        w.row("repr", x, repr(x))
    for _ in range(w.pick(quick=1500, full=500000)):
        x = engine_double(rng)
        w.row("repr", x, repr(x))
    for _ in range(w.pick(quick=500, full=100000)):
        x = vecutil.rand_double(rng) if rng.random() < 0.5 else engine_double(rng)
        d, p = shortest(x)
        w.row("short", x, d, p, math.copysign(1.0, x) < 0)


def gen_prec(w):
    rng = w.rng
    for x in edge_doubles(w.full):
        if not finite(x):
            continue
        for p in (1, 2, 7, 15, 16, 17, 18, 25, 40, 800):
            d, e = prec_digits(x, p)
            w.row("prec", x, p, d, e, math.copysign(1.0, x) < 0)
    for _ in range(w.pick(quick=1500, full=300000)):
        x = vecutil.rand_double(rng) if rng.random() < 0.5 else engine_double(rng)
        r = rng.random()
        p = rng.randint(1, 20) if r < 0.8 else rng.randint(1, 120) if r < 0.97 else \
            rng.randint(1, 800)
        d, e = prec_digits(x, p)
        w.row("prec", x, p, d, e, math.copysign(1.0, x) < 0)
    # ties at p significant digits: small integers and binary fractions
    for _ in range(w.pick(quick=300, full=50000)):
        p = rng.randint(1, 16)
        x = (rng.randrange(1, 10 ** (p + 1)) * 10 + 5) / 10 ** rng.randint(0, 2)
        if rng.random() < 0.5:
            x = rng.randrange(1, 1 << 40) / (1 << rng.randint(1, 30))
        d, e = prec_digits(x, p)
        w.row("prec", x, p, d, e, x < 0)


def write_fixed(w, x, places, grouping):
    spec = "%s.%df" % ("," if grouping else "", places)
    w.row("fixed", x, places, grouping, format(x, spec))


def gen_fixed(w):
    rng = w.rng
    for x in edge_doubles(w.full):
        for places in (0, 1, 2, 3, 7, 16, 20):
            if finite(x) and abs(x) > 1e30 and places > 2:
                continue
            write_fixed(w, x, places, places % 2 == 0)
    for x in (DBL_MAX, -DBL_MAX, TINY, 1e-300, 0.1, -0.0):
        for places in (100, 330, 400):
            write_fixed(w, x, places, True)
    for _ in range(w.pick(quick=2500, full=500000)):
        x = engine_double(rng)
        if abs(x) > 1e40:
            x = rng.uniform(-1e12, 1e12)
        places = rng.randint(0, 20) if rng.random() < 0.9 else rng.randint(0, 60)
        write_fixed(w, x, places, rng.random() < 0.7)
    # exact ties at `places`: odd multiples of 2**-(places + 1)
    for _ in range(w.pick(quick=500, full=100000)):
        places = rng.randint(0, 20)
        x = rng.randrange(-(1 << 50), 1 << 50) * 2 + 1
        x = x / (1 << (places + 1)) / (1 << rng.randint(0, 2))
        write_fixed(w, x, places, rng.random() < 0.5)


def gen_g(w):
    rng = w.rng
    for x in edge_doubles(w.full):
        for p in (0, 1, 2, 6, 7, 15, 16, 17, 20):
            w.row("g", x, p, format(x, ".%dg" % p))
    for _ in range(w.pick(quick=2000, full=300000)):
        x = vecutil.rand_double(rng) if rng.random() < 0.4 else engine_double(rng)
        if rng.random() < 0.2:
            x = x * 10.0 ** rng.randint(-30, -10)
        r = rng.random()
        p = rng.choice((7, 15, 16)) if r < 0.6 else rng.randint(1, 25) if r < 0.97 else \
            rng.randint(25, 400)
        w.row("g", x, p, format(x, ".%dg" % p))


def write_round(w, x, n):
    w.row("round", x, n, vecutil.result(round, x, n))


def gen_round(w):
    rng = w.rng
    ns = (-400, -309, -308, -307, -300, -20, -2, -1, 0, 1, 2, 3, 7, 15, 16, 17, 20, 300, 322,
          323, 324, 330, 10 ** 18, -(10 ** 18))
    if not w.full:
        ns = (-309, -308, -2, 0, 1, 2, 7, 16, 323, 324, -(10 ** 18))
    for x in edge_doubles(w.full):
        for n in ns:
            write_round(w, x, n)
    for _ in range(w.pick(quick=3000, full=500000)):
        x = engine_double(rng)
        r = rng.random()
        n = rng.randint(-3, 20) if r < 0.8 else rng.randint(-25, 40) if r < 0.95 else \
            rng.randint(-330, 330)
        write_round(w, x, n)
    for _ in range(w.pick(quick=1000, full=150000)):
        x = vecutil.rand_double(rng)
        e = math.frexp(x)[1]
        n = rng.randint(-330, 330) if rng.random() < 0.3 else int(-e * 0.30103) + \
            rng.randint(-3, 20)
        write_round(w, x, n)
    # exact ties at n places
    for _ in range(w.pick(quick=500, full=100000)):
        n = rng.randint(0, 20)
        x = (rng.randrange(-(1 << 45), 1 << 45) * 2 + 1) / (1 << (n + 1))
        write_round(w, x, n)
        m = rng.randint(1, 5)
        write_round(w, float((rng.randrange(-10 ** 8, 10 ** 8) * 2 + 1) * 5 * 10 ** (m - 1)), -m)


def write_parse(w, s):
    if GRAMMAR.match(s) is None:
        try:
            float(s)
        except ValueError:
            w.row("parse", s, vecutil.Exc("ValueError"))
        return
    w.row("parse", s, float(s))


def gen_parse(w):
    rng = w.rng
    fixed = ["0", "-0", "+0", "0.0", "-0.0", ".0", "0.", "5.", ".5", "-.5", "+5.", "5.e3",
             ".5e3", "1e308", "1.7976931348623157e308", "1.7976931348623158e308",
             "1.7976931348623159e308", "179769313486231580793728971405303415079934132710037826"
             "936173778980444968292764750946649017977587207096330286416692887910946555547851940"
             "402630657488671505820681908902000708383676273854845817711531764475730270069855571"
             "366959622842914819860834936475292719074168444365510704342711559699508093042880177"
             "904174497791.9999999999", "2.4703282292062327e-324", "2.4703282292062328e-324",
             "2.47032822920623272e-324", "4.9406564584124654e-324", "1e-324", "3e-324",
             "1e-400", "-1e-400", "1e400", "-1e400", "1e99999999999999999999",
             "1e-99999999999999999999", "0e99999999999999999999", "0.000000000000e-5",
             "1E5", "1e+5", "1e-5", "1e05", "9007199254740993", "9007199254740992.5",
             "9007199254740993.0000000000000000000001", "123456789012345678901234567890",
             "0.1", "0.2", "0.3", "1.005", "2.675", "100", "1000000000000000000000",
             "1" * 400, "9" * 400, "0." + "0" * 400 + "1", "1" + "0" * 308,
             "1" + "0" * 309, "0." + "0" * 323 + "247032822920623272", "1" * 10000,
             "0." + "0" * 9990 + "1" * 9, "1" * 5000 + "e-5000", "1" * 5000 + "e-4700",
             "8.41e21", "1e23", "2.2250738585072011e-308", "2.2250738585072012e-308",
             "", ".", "-", "+", "e5", ".e5", "1e", "1e+", "1e-", "1..5", "1.2.3", "--1", "+-1",
             "1e5.5", "1ee5", "0x10", "1,000", "abc", "1.5x", "e", "E+1"]
    for s in fixed:
        write_parse(w, s)
    for x in edge_doubles(w.full):
        if finite(x):
            write_parse(w, repr(x))
            write_parse(w, format(x, ".17g"))
            write_parse(w, format(x, ".25e"))
            if x > 0:
                for t in midpoint_texts(rng, x):
                    write_parse(w, t)
    for _ in range(w.pick(quick=1500, full=200000)):
        write_parse(w, rand_digits_text(rng))
    for _ in range(w.pick(quick=300, full=40000)):
        x = abs(vecutil.rand_double(rng)) if rng.random() < 0.5 else abs(engine_double(rng))
        if x > 0 and finite(x):
            for t in midpoint_texts(rng, x):
                write_parse(w, t)
    for _ in range(w.pick(quick=500, full=50000)):
        x = vecutil.rand_double(rng)
        write_parse(w, rng.choice((repr(x), format(x, ".17e"), format(x, ".16g"))))


def write_dec(w, s):
    with decimal.localcontext() as c:
        c.prec = 400
        try:
            d = Decimal(s)
        except decimal.DecimalException as e:
            exc = vecutil.Exc(vecutil.exc_name(e))
            w.row("dec", s, exc, exc, exc, exc, exc, exc, exc)
            return
        norm = vecutil.result(d.normalize)
        if not isinstance(norm, vecutil.Exc):
            norm = dec_triple(norm)
        # Fixed notation of a huge exponent is a huge string: not compared (null).
        small = abs(d.as_tuple().exponent) <= 100000
        w.row("dec", s, dec_triple(d), str(d), norm, len(d.as_tuple().digits), float(d),
              format(d, "f") if small else None, format(d, ",f") if small else None)


def rand_dec_text(rng):
    r = rng.random()
    if r < 0.6:
        nd = rng.randint(1, 30)
    elif r < 0.95:
        nd = rng.randint(30, 120)
    else:
        nd = rng.randint(120, 500)
    digits = "".join(rng.choice("0123456789") for _ in range(nd))
    if rng.random() < 0.2:
        digits = "0" * rng.randint(1, 5) + digits
    if rng.random() < 0.3:
        digits += "0" * rng.randint(1, 10)
    point = rng.randint(0, len(digits))
    text = digits[:point] + "." + digits[point:] if rng.random() < 0.6 else digits
    if rng.random() < 0.4:
        e = rng.randint(-30, 30)
        text += rng.choice("eE") + str(e)
    if rng.random() < 0.3:
        text = "-" + text
    return text


def gen_dec(w):
    rng = w.rng
    fixed = ["0", "-0", "0.00", "-0.00", "0E-7", "0e5", "-0e+5", "1E-7", "1e-6", "100", "1.5",
             "007", ".5", "5.", "-.5", "1E+2", "6.022E+23", "123.0", "1e999999999999999999",
             "1e1000000000000000000", "10e999999999999999999", "1e-1999999999999999997",
             "1e-1999999999999999998", "0.00e-1999999999999999997",
             "0.0e1000000000000000000", "1e99999999999999999999", "1" * 1000, "9" * 450,
             "1" + "0" * 450, "1." + "0" * 450, "0." + "0" * 30 + "1", "1.234567e-20",
             "-1e-15", "4.940656e-324", "", ".", "1e", "1.2.3", "abc", "e5", "1e+"]
    for s in fixed:
        write_dec(w, s)
    for _ in range(w.pick(quick=1000, full=100000)):
        write_dec(w, rand_dec_text(rng))
    for x in edge_doubles(w.full):
        if finite(x):
            for p in (15, 16):
                w.row("decg", x, p, dec_triple(Decimal(format(x, ".%dg" % p))))
            w.row("decrepr", x, dec_triple(Decimal(repr(x))))
    for _ in range(w.pick(quick=1500, full=200000)):
        x = vecutil.rand_double(rng) if rng.random() < 0.3 else engine_double(rng)
        p = rng.choice((15, 16)) if rng.random() < 0.8 else rng.randint(1, 20)
        w.row("decg", x, p, dec_triple(Decimal(format(x, ".%dg" % p))))
        w.row("decrepr", x, dec_triple(Decimal(repr(x))))


def write_quant(w, s, places):
    with decimal.localcontext() as c:
        c.prec = 400
        d = Decimal(s)
        try:
            q = d.quantize(Decimal(1).scaleb(-places), rounding=decimal.ROUND_HALF_UP)
        except decimal.DecimalException as e:
            exc = vecutil.Exc(vecutil.exc_name(e))
            w.row("quant", s, places, exc, exc)
            return
        w.row("quant", s, places, dec_triple(q), format(q, ",.%df" % places))


def write_fmtf(w, s, places, grouping):
    with decimal.localcontext() as c:
        c.prec = 400
        d = Decimal(s)
        w.row("fmtf", s, places, grouping, format(d, "%s.%df" % ("," if grouping else "", places)))


def gen_quant(w):
    rng = w.rng
    for s in ("1.005", "2.675", "0.125", "-0.125", "0.5", "-0.5", "1.5", "2.5", "0", "-0",
              "0.001", "-0.001", "9.995", "99.995", "-99.995", "1e300", "1" * 399, "1" * 400,
              "1" * 401, "9" * 399 + ".5", "123456.785", "0.0049999", "1E+2", "-1E-20"):
        for places in (0, 2, 4, 6):
            write_quant(w, s, places)
            write_fmtf(w, s, places, True)
            write_fmtf(w, s, places, False)
    write_quant(w, "1", 1000000)
    write_quant(w, "1", -1000000)
    write_quant(w, "1", -1000398)
    write_quant(w, "1", -1000399)
    for _ in range(w.pick(quick=1500, full=150000)):
        r = rng.random()
        if r < 0.4:
            x = engine_double(rng)
            if not finite(x) or abs(x) > 1e300:
                continue
            s = format(x, ".15g") if rng.random() < 0.6 else repr(x)
        else:
            s = rand_dec_text(rng)
        places = rng.choice((0, 2, 4, 6)) if rng.random() < 0.7 else rng.randint(0, 25)
        write_quant(w, s, places)
        write_fmtf(w, s, places, rng.random() < 0.5)


def write_arith(w, case, a, b, prec):
    with decimal.localcontext() as c:
        c.prec = prec
        da, db = Decimal(a), Decimal(b)
        try:
            r = da + db if case == "add" else da / db
        except decimal.DecimalException as e:
            exc = vecutil.Exc(vecutil.exc_name(e))
            w.row(case, a, b, prec, exc, exc)
            return
        w.row(case, a, b, prec, dec_triple(r), str(r))


def small_dec_text(rng):
    if rng.random() < 0.5:
        return str(rng.randrange(0, 10 ** rng.randint(1, 30)))
    return rand_dec_text(rng)


def gen_arith(w):
    rng = w.rng
    fixed = [("1", "3", 28), ("5", "0.3333333333333333333333333333", 28), ("1", "1000000000", 28),
             ("1", "30000000", 28), ("6", "3", 28), ("100", "1", 28), ("15", "10", 28),
             ("0", "5", 28), ("0", "0", 28), ("1", "0", 28), ("-1", "3", 28), ("0.00", "1", 28),
             ("1E+40", "1", 28), (str(10 ** 40), "1", 28), ("1", "2", 10020),
             ("1", "1024", 28), ("-0", "5", 28), ("2", "-0", 28)]
    for a, b, p in fixed:
        write_arith(w, "div", a, b, p)
        write_arith(w, "add", a, b, p)
    for a, b, p in [("1E+50", "1E-50", 28), ("1E+50", "-1E-50", 28), ("1", "-1", 28),
                    ("-0", "-0", 28), ("-0", "0", 28), ("0E-1000", "1", 28), ("1", "0E-10", 28),
                    ("1" * 40, "0E-5", 28), ("1" * 40, "5E-100", 28),
                    ("1000000000000000000000000000049999", "1E-40", 28),
                    ("9" * 28, "0.5", 28), ("9" * 28, "0.49999", 28), ("9" * 28, "-0.5", 28),
                    ("1E+30", "-1E-30", 5), ("5E+30", "-1E-30", 1), ("1", "1E-29", 28),
                    ("1", "-1E-28", 28), ("1.5", "1E-400", 28), ("2.5", "1E-400", 1),
                    ("2.5", "-1E-400", 1), ("0.5", "1E-400", 1)]:
        write_arith(w, "add", a, b, p)
    for _ in range(w.pick(quick=1500, full=150000)):
        prec = rng.choice((28, 28, 30, 48)) if rng.random() < 0.6 else rng.randint(1, 60)
        a = small_dec_text(rng)
        b = small_dec_text(rng)
        write_arith(w, "div", a, b, prec)
        write_arith(w, "add", a, b, prec)
    # an operand at or just below the last digit of one with more digits than prec, often
    # next to a tie: the sticky reduction must not apply there
    for a, b, p in [("1235499", "1", 4), ("1235499", "1E-1", 4), ("1235501", "-1", 4),
                    ("1234500", "1E-10", 4), ("1234500", "-1E-10", 4), ("1235500", "-1E-10", 4)]:
        write_arith(w, "add", a, b, p)
    for _ in range(w.pick(quick=500, full=50000)):
        prec = rng.randint(1, 10)
        na = prec + rng.randint(1, 8)
        ca = rng.randrange(10 ** (na - 1), 10 ** na)
        if rng.random() < 0.6:
            drop = na - prec
            ca = ca // 10 ** drop * 10 ** drop + 5 * 10 ** (drop - 1) + rng.randint(-2, 2)
        ea = rng.randint(-20, 20)
        nb = rng.randint(1, 3)
        cb = rng.randrange(1, 10 ** nb)
        eb = ea - nb + 1 + rng.randint(-4, 1)
        a = "%s%de%d" % (rng.choice(("", "-")), ca, ea)
        b = "%s%de%d" % (rng.choice(("", "-")), cb, eb)
        write_arith(w, "add", a, b, prec)
        write_arith(w, "add", b, a, prec)
    # far apart exponents for the addition
    for _ in range(w.pick(quick=500, full=50000)):
        prec = rng.randint(1, 40)
        a = "%de%d" % (rng.randrange(-10 ** 12, 10 ** 12), rng.randint(-60, 60))
        b = "%de%d" % (rng.randrange(-10 ** 12, 10 ** 12), rng.randint(-200, 200))
        write_arith(w, "add", a, b, prec)


def gen_engine(w):
    calc = vecutil.import_calc()
    from calc.operator_ import _decimals
    from calc.value import Value

    rng = w.rng
    fractions = [(1, 2), (1, 3), (2, 3), (1, 4), (3, 4), (1, 5), (2, 5), (3, 5), (4, 5), (1, 6),
                 (5, 6), (1, 7), (1, 8), (3, 8), (5, 8), (7, 8), (1, 9), (1, 10)]
    for whole in (None, "0", "5", "007", "1,000", "1,2,3", "9" * 400, "123,456,789"):
        for n, d in fractions:
            w.row("mixed", whole, n, d, calc.Calc._mixed(whole, n, d))
    w.row("mixed", "1" * 5000, 1, 3, calc.Calc._mixed("1" * 5000, 1, 3))
    for _ in range(w.pick(quick=600, full=60000)):
        whole = None if rng.random() < 0.3 else str(rng.randrange(0, 10 ** rng.randint(1, 40)))
        if whole is not None and rng.random() < 0.3:
            whole = "{:,}".format(int(whole))
        nl = rng.randint(1, 15)
        n = rng.randrange(0, 10 ** nl)
        d = rng.randrange(1, 10 ** rng.randint(1, 30 - nl))
        w.row("mixed", whole, n, d, calc.Calc._mixed(whole, n, d))

    vals = [0, 1, -1, 5, 10 ** 300, -(10 ** 300), int(DBL_MAX)] + [
        x for x in edge_doubles(w.full) if finite(x)]
    for x in vals:
        for places in (0, 2, 4, 6):
            w.row("fixedv", x, places, Value._fixed(x, places))
    for _ in range(w.pick(quick=1500, full=150000)):
        x = engine_double(rng)
        if not finite(x):
            continue
        if rng.random() < 0.1:
            x = rng.randrange(-10 ** 20, 10 ** 20)
        places = rng.choice((0, 2, 2, 4, 6))
        w.row("fixedv", x, places, Value._fixed(x, places))

    big = [2.0 ** 53, -(2.0 ** 53), 6.022e23, 1e300, DBL_MAX, -DBL_MAX, 1e16, 1e17, 1e22, 1e23,
           123456789012345680.0, 9007199254740994.0, 2.0 ** 63, 2.0 ** 1023]
    for _ in range(w.pick(quick=1000, full=100000)):
        x = vecutil.rand_double(rng)
        if finite(x) and abs(x) >= 2 ** 53:
            big.append(x)
        x = short_decimal(rng, 17, 30)
        if abs(x) >= 2 ** 53:
            big.append(x)
    for x in big:
        w.row("shownbig", x, Value._shown(x))

    small = [1e-15, -1e-15, 1.234567e-20, 5e-324, -5e-324, 1e-300, 4.94e-322, 1e-21, 3.3e-21]
    for _ in range(w.pick(quick=500, full=50000)):
        small.append(short_decimal(rng, 17, 1) * 10.0 ** rng.randint(-320, -14))
    for x in small:
        s = format(x, ".7g")
        with decimal.localcontext() as c:
            c.prec = 400
            w.row("english", x, s, f"{Decimal(s):f}")

    for x in edge_doubles(w.full):
        if finite(x):
            w.row("decimals", x, _decimals(x))
    for _ in range(w.pick(quick=1000, full=100000)):
        x = engine_double(rng)
        if finite(x):
            w.row("decimals", x, _decimals(x))


def write_json(w, lit):
    w.row("json", lit, vecutil.result(json.loads, lit))


def gen_json(w):
    rng = w.rng
    for lit in ("0", "-0", "1", "-1", "0.0", "-0.0", "1e400", "-1e400", "1E2", "1e-2", "0e0",
                "-0E-0", "NaN", "Infinity", "-Infinity", "9223372036854775807",
                "9223372036854775808", "-9223372036854775808", "-9223372036854775809",
                "1" * 4300, "1" * 4301, "-" + "1" * 4300, "-" + "1" * 4301, "1" * 400 + ".5",
                "1" * 400 + "e-400", "123.456e7", "1.7976931348623157e308",
                "1.7976931348623159e308", "5e-324", "2e-324", "0.1"):
        write_json(w, lit)
    for _ in range(w.pick(quick=1000, full=100000)):
        r = rng.random()
        if r < 0.3:
            v = vecutil.rand_int(rng, rng.choice((10, 63, 64, 200, 2000)))
            lit = str(v)
        elif r < 0.6:
            x = vecutil.rand_double(rng) if rng.random() < 0.5 else engine_double(rng)
            lit = json.dumps(x)
            if not GRAMMAR.match(lit):
                continue
        else:
            lit = rand_digits_text(rng).lstrip("+")
            # JSON's own grammar: no leading zeros, digits on both sides of the point
            lit = re.sub(r"^(-?)0+(?=[0-9])", r"\1", lit)
            if not re.fullmatch(r"-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?", lit):
                continue
        write_json(w, lit)


def gen_int(w):
    """int(s, base) as special_tokens.py calls it: digit runs up to the 10,000-character line."""
    rng = w.rng
    for s, base in (("0", 16), ("000", 2), ("ff", 16), ("FF", 16), ("777", 8), ("102", 2),
                    ("1" * 4300, 10), ("1" * 4301, 10), ("0" * 4301, 10), ("f" * 10000, 16),
                    ("7" * 10000, 8), ("1" * 10000, 2), ("g", 16), ("8", 8)):
        w.row("int", s, base, vecutil.result(int, s, base))
    for _ in range(w.pick(quick=300, full=5000)):
        base = rng.choice((2, 8, 10, 16))
        n = rng.randint(1, 40) if rng.random() < 0.8 else rng.randint(40, 10000)
        if base == 10:
            n = min(n, 4300)
        s = "".join(rng.choice("0123456789abcdefABCDEF"[:base + (6 if base == 16 else 0)])
                    for _ in range(n))
        w.row("int", s, base, int(s, base))


def main():
    with vecutil.Writer("floats") as w:
        gen_repr(w)
        gen_prec(w)
        gen_fixed(w)
        gen_g(w)
        gen_round(w)
        gen_parse(w)
        gen_dec(w)
        gen_quant(w)
        gen_arith(w)
        gen_engine(w)
        gen_json(w)
        gen_int(w)


if __name__ == "__main__":
    main()
