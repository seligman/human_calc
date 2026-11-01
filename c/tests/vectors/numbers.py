"""Vectors for tests/rt/test_numbers.c: Python int and float semantics of src/rt/py_int.c,
py_num.c, py_float.c and py_math.c (DESIGN.md 5.4, 13.3).

Numbers are vecutil-encoded (ints exact, floats by their bits). A result column is the value,
{"exc": "<Class>"}, or the string "complex" (a complex result, which the C side only flags).

Rows (columns after the case name):
    op      name, a, b, operator.<name>(a, b); name is one of add sub mul truediv floordiv mod
            pow lshift rshift and or xor (floordiv: ints only, the engine's only use)
    un      name, a, result: neg abs (-a, abs(a)), float (float(a)), valid (abs(a) <=
            sys.float_info.max), truthy (bool), is_integer (float.is_integer, True for ints),
            sign ((a > 0) - (a < 0)), to_i64 (a, or null outside int64), sat (a clamped to
            int64, floats truncated first, nan 0), is_odd (ints), trunc round floor ceil
            (int(x), round(x), math.floor(x), math.ceil(x) for a float x); float rows for
            ints only (float() of a float is the identity)
    cmp     a, b, -1 / 0 / 1, or 2 when no ordering holds (nan)
    max     a, b, max(a, b)
    min     a, b, min(a, b)
    str     v, radix, grouping, f"{v:,}" / str(v) / format(v, "x" | "o" | "b")
    parse   digits, radix, negative, int(("-" if negative else "") + digits, radix)
    fact    n, math.factorial(n)
    ratio   num, den, raises, num / den (with raises false an OverflowError becomes +-inf)
    intinfo v, v.bit_length(), v % 2 == 1, (v > 0) - (v < 0)
    frem    x, y, x % y (floats)
    fpow    x, y, x ** y (floats)
    fisint  x, x.is_integer()
    math    name, x, math.<name>(x) for sqrt log log10 log2 sin cos tan asin acos atan degrees
            radians
    math2   name, x, y, math.<name>(x, y) for fmod copysign
"""

import vecutil

import math
import operator
import sys

INF = float("inf")
NAN = float("nan")
DBL_MAX = sys.float_info.max
I64_MIN = vecutil.INT64_MIN
I64_MAX = vecutil.INT64_MAX

OPS = {
    "add": operator.add,
    "sub": operator.sub,
    "mul": operator.mul,
    "truediv": operator.truediv,
    "floordiv": operator.floordiv,
    "mod": operator.mod,
    "pow": operator.pow,
    "lshift": operator.lshift,
    "rshift": operator.rshift,
    "and": operator.and_,
    "or": operator.or_,
    "xor": operator.xor,
}

SPECIAL_FLOATS = [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 2.5, -2.5, 1 / 3,
                  INF, -INF, NAN, 1e308, -1e308, DBL_MAX, -DBL_MAX, 5e-324, -5e-324, 1e-300,
                  10.0, -7.5, 360.0, 90.0, -90.0, 0.1, 1e16, -1e16, 2.0 ** 63, -(2.0 ** 63),
                  2.0 ** 53, 2.0 ** 53 + 2, 4503599627370495.5, -4503599627370495.5]

SPECIAL_INTS = [0, 1, -1, 2, -2, 3, -3, 5, -5, 7, 10, 100, 255, -256, 1023, 1024, 2048, 2049,
                I64_MAX, I64_MIN, I64_MAX + 1, I64_MIN - 1, 1 << 53, (1 << 53) + 1, -(1 << 53) - 1,
                1 << 64, -(1 << 64), 10 ** 30, -(10 ** 30), int(DBL_MAX), int(DBL_MAX) + 1,
                -int(DBL_MAX) - 1, (1 << 1024) - (1 << 970), (1 << 1024) - (1 << 970) - 1,
                1 << 1024, 10 ** 400, -(10 ** 400), 0x20000000000001 * 3]


def fix(v):
    """A result for enc(): complex becomes the "complex" marker."""
    if isinstance(v, complex):
        return vecutil.Raw("complex")
    return v


def res(fn, *args):
    return fix(vecutil.result(fn, *args))


def rand_int(rng, max_bits):
    return vecutil.rand_int(rng, max_bits)


def rand_float(rng):
    r = rng.random()
    if r < 0.35:
        return vecutil.rand_double(rng)
    if r < 0.55:
        return rng.uniform(-1e6, 1e6)
    if r < 0.65:
        return float(rng.randint(-10 ** 6, 10 ** 6))
    if r < 0.72:
        return rng.randint(-10 ** 6, 10 ** 6) + 0.5
    if r < 0.8:
        return rng.choice(SPECIAL_FLOATS)
    if r < 0.9:
        return rng.uniform(-10, 10) * 10.0 ** rng.randint(-320, 308)
    return float(rand_int(rng, 1023) or 1) if rng.random() < 0.5 else rng.uniform(-1, 1)


def rand_any_int(rng):
    r = rng.random()
    if r < 0.25:
        return rng.randint(-1000, 1000)
    if r < 0.45:
        return rand_int(rng, 64)
    if r < 0.6:
        return rng.choice(SPECIAL_INTS)
    if r < 0.85:
        return rand_int(rng, 200)
    return rand_int(rng, 1100)


def rand_operand(rng, kind):
    return rand_any_int(rng) if kind == "i" else rand_float(rng)


def write_op(w, name, a, b):
    w.row("op", name, a, b, res(OPS[name], a, b))


def gen_ops(w):
    rng = w.rng
    n = w.pick(quick=150, full=6000)
    kinds = ("ii", "if", "fi", "ff")
    for name in ("add", "sub", "mul", "truediv", "mod"):
        for _ in range(n):
            k = rng.choice(kinds)
            write_op(w, name, rand_operand(rng, k[0]), rand_operand(rng, k[1]))
        # small divisors and zero divisors
        for _ in range(n // 5):
            a = rand_operand(rng, rng.choice("if"))
            b = rng.choice([0, 0.0, -0.0, 1, -1, 2, -2, 3, 0.1, -0.1, 7.0, 1e-300, 1e300])
            write_op(w, name, a, b)
    # int / int over every size, including quotients that underflow or overflow
    for _ in range(n * 2):
        a = rand_int(rng, rng.choice((60, 300, 1200, 2200)))
        b = rand_int(rng, rng.choice((60, 300, 1200, 2200))) or 1
        write_op(w, "truediv", a, b)
    for _ in range(n):
        a = rand_int(rng, 400)
        b = rand_int(rng, 400) or 3
        write_op(w, "floordiv", a, b)
        write_op(w, "mod", a, b)
        write_op(w, "floordiv", rng.randint(-50, 50), rng.choice((1, -1, 2, -2, 3, -3, 7)))
    for a in SPECIAL_INTS:
        for b in (0, 1, -1, 2, -2, 3, -3, 7, I64_MIN, I64_MAX, 10 ** 30, -(10 ** 30)):
            write_op(w, "floordiv", a, b)
            write_op(w, "mod", a, b)
    # pow: exact ints (results kept below about 6000 bits), int ** negative, floats
    for _ in range(n * 2):
        k = rng.choice(kinds)
        a = rand_operand(rng, k[0])
        if k == "ii":
            b = rng.randint(-40, 200)
            if abs(a) > 1:
                b = min(b, 6000 // max(1, abs(a).bit_length()))
        elif k[1] == "i":
            b = rng.randint(-400, 400)
        else:
            b = rand_operand(rng, k[1])
        write_op(w, "pow", a, b)
    for a in (0, 1, -1, 2, -2, 3, 10, -10, 1 << 70):
        for b in (0, 1, 2, 3, 63, 64, 100, -1, -2, -3, I64_MAX, I64_MAX + 1, 10 ** 30,
                  10 ** 30 + 1, -(10 ** 30), -(10 ** 400)):
            if abs(a) > 1 and b > 3000:
                continue
            write_op(w, "pow", a, b)
    for a in (2, -2, 3, 10, 7):
        for b in (2.0, 3.0, 0.5, -0.5, 1 / 3, 1500.0, -1500.0):
            write_op(w, "pow", a, b)
            write_op(w, "pow", float(a), b)
            write_op(w, "pow", -float(a), b)
    # shifts and bitwise ops
    for _ in range(n * 2):
        a = rand_any_int(rng)
        write_op(w, "lshift", a, rng.randint(-3, 130) if rng.random() < 0.7 else
                 rng.randint(0, 3000))
        write_op(w, "rshift", a, rng.randint(-3, 130) if rng.random() < 0.7 else
                 rand_any_int(rng))
        b = rand_any_int(rng)
        for name in ("and", "or", "xor"):
            write_op(w, name, a, b)
    for a in SPECIAL_INTS:
        for b in (0, 1, 62, 63, 64, 2048, -1, 10 ** 30, I64_MAX, I64_MAX + 1):
            if a == 0 or b <= 3000:
                write_op(w, "lshift", a, b)
            write_op(w, "rshift", a, b)
    for a in (1.0, 2.5, -1.0):
        for name in ("lshift", "rshift", "and", "or", "xor"):
            write_op(w, name, a, 1)
            write_op(w, name, 1, a)
    # special values in every combination
    pool = SPECIAL_FLOATS + [0, 1, -1, 2, 3, 10 ** 400, -(10 ** 400), 1 << 63, int(DBL_MAX)]
    if not w.full:
        pool = [0.0, -0.0, 1.0, -1.0, 0.5, -2.5, INF, -INF, NAN, DBL_MAX, 5e-324, -1e308, 0, 1,
                -1, 3, 10 ** 400, -(10 ** 400)]
    for name in ("add", "sub", "mul", "truediv", "mod", "pow"):
        for a in pool:
            for b in pool:
                if (name == "pow" and isinstance(a, int) and isinstance(b, int) and abs(a) > 1
                        and b > 3000):
                    continue
                write_op(w, name, a, b)


def gen_unary(w):
    rng = w.rng
    n = w.pick(quick=150, full=8000)
    ints = SPECIAL_INTS + [rand_any_int(rng) for _ in range(n)]
    for k in range(-5, 1100, w.pick(97, 1)):
        base = 1 << k if k >= 0 else 1
        ints += [base, base - 1, base + 1, -base]
    # round-to-even and sticky-bit cases of float(int)
    for k in range(0, 1000, w.pick(97, 2)):
        for m in ((1 << 53) + 1, (1 << 53) + 3, (1 << 54) - 1, (1 << 53) + (1 << 52) + 1):
            ints += [m << k, (m << k) + 1, (m << k) - 1, -(m << k)]
    floats = vecutil.edge_doubles()[:: w.pick(3, 1)] + SPECIAL_FLOATS
    floats += [rand_float(rng) for _ in range(n)]
    for k in range(w.pick(20, 1500)):
        floats += [rng.randint(-100, 100) + 0.5, rng.randint(-(1 << 52), 1 << 52) + 0.5,
                   float(rng.randint(-(1 << 63), 1 << 63)), rng.uniform(-3, 3)]
    for x in ints:
        w.row("un", "neg", x, -x)
        w.row("un", "abs", x, abs(x))
        w.row("un", "float", x, res(float, x))
        w.row("un", "valid", x, abs(x) <= DBL_MAX)
        w.row("un", "truthy", x, bool(x))
        w.row("un", "is_integer", x, True)
        w.row("un", "sign", x, (x > 0) - (x < 0))
        w.row("un", "to_i64", x, x if I64_MIN <= x <= I64_MAX else None)
        w.row("un", "sat", x, min(max(x, I64_MIN), I64_MAX))
        w.row("un", "is_odd", x, x % 2 == 1)
        w.row("intinfo", x, x.bit_length(), x % 2 == 1, (x > 0) - (x < 0))
    for x in floats:
        w.row("un", "neg", x, -x)
        w.row("un", "abs", x, abs(x))
        w.row("un", "valid", x, abs(x) <= DBL_MAX)
        w.row("un", "truthy", x, bool(x))
        w.row("un", "is_integer", x, x.is_integer())
        w.row("un", "sign", x, (x > 0) - (x < 0))
        if x != x:
            sat = 0
        elif x in (INF, -INF):
            sat = I64_MAX if x > 0 else I64_MIN
        else:
            sat = min(max(int(x), I64_MIN), I64_MAX)
        w.row("un", "sat", x, sat)
        w.row("un", "trunc", x, res(int, x))
        w.row("un", "round", x, res(round, x))
        w.row("un", "floor", x, res(math.floor, x))
        w.row("un", "ceil", x, res(math.ceil, x))
        w.row("fisint", x, x.is_integer())


def gen_cmp(w):
    rng = w.rng
    n = w.pick(quick=300, full=30000)
    pairs = []
    for _ in range(n):
        kind = rng.choice(("ii", "if", "fi", "ff"))
        pairs.append((rand_operand(rng, kind[0]), rand_operand(rng, kind[1])))
    for _ in range(n):
        x = rand_float(rng)
        if x != x or x in (INF, -INF):
            continue
        i = int(x) + rng.randint(-2, 2)
        pairs += [(i, x), (x, i)]
    pool = SPECIAL_INTS + SPECIAL_FLOATS + [2 ** 63 - 1, -(2 ** 63), 2 ** 1023, 2.0 ** 1023,
                                             float(2 ** 63 - 1), 9007199254740993, 9007199254740992.0]
    pool = pool[:: w.pick(2, 1)]
    for a in pool:
        for b in pool:
            pairs.append((a, b))
    for a, b in pairs:
        if a < b:
            c = -1
        elif a > b:
            c = 1
        elif a == b:
            c = 0
        else:
            c = 2
        w.row("cmp", a, b, c)
    for a, b in pairs[: w.pick(300, 10000)]:
        w.row("max", a, b, max(a, b))
        w.row("min", a, b, min(a, b))
    for a, b in ((1, 1.0), (1.0, 1), (-0.0, 0.0), (0.0, -0.0), (NAN, 1), (1, NAN), (NAN, 1.0),
                 (2.0, NAN), (0, -0.0), (-0.0, 0)):
        w.row("max", a, b, max(a, b))
        w.row("min", a, b, min(a, b))


def gen_text(w):
    rng = w.rng
    n = w.pick(quick=100, full=3000)
    vals = SPECIAL_INTS + [rand_any_int(rng) for _ in range(n)]
    vals += [10 ** 4299, 10 ** 4300 - 1, 10 ** 4300, -(10 ** 4300 - 1), -(10 ** 4300),
             1 << 14300, (1 << 14300) - 1, 1 << 14301, 10 ** 4305]
    for _ in range(w.pick(6, 200)):
        vals.append(rand_int(rng, 15000))
    for v in vals:
        for radix, grouping in ((10, False), (10, True), (16, False), (8, False), (2, False)):
            if radix == 10:
                fn = (lambda x: f"{x:,}") if grouping else str
            else:
                spec = {16: "x", 8: "o", 2: "b"}[radix]
                fn = (lambda x, s=spec: format(x, s))
            if radix == 2 and v.bit_length() > 6000:
                continue
            w.row("str", v, radix, grouping, res(fn, v))
    w.row("str", 255, 16, True, vecutil.Exc("ValueError"))
    digit_sets = {2: "01", 8: "01234567", 10: "0123456789", 16: "0123456789abcdefABCDEF"}
    texts = []
    for radix, digits in digit_sets.items():
        for _ in range(n):
            length = rng.choice((1, 2, 5, 15, 18, 19, 20, 21, 40, 100, 600))
            s = "".join(rng.choice(digits) for _ in range(length))
            if rng.random() < 0.2:
                s = "0" * rng.randint(1, 30) + s
            texts.append((s, radix))
        for s in ("0", "00", "1", "7", "9223372036854775807", "9223372036854775808",
                  "9223372036854775809", "18446744073709551615", "18446744073709551616"):
            if all(c in digits for c in s):
                texts.append((s, radix))
    texts += [("7fffffffffffffff", 16), ("8000000000000000", 16), ("8000000000000001", 16),
              ("ffffffffffffffff", 16), ("1" + "0" * 63, 2), ("1" + "0" * 64, 2),
              ("777777777777777777777", 8), ("1000000000000000000000", 8),
              ("1" * 4300, 10), ("1" * 4301, 10), ("0" * 4301, 10), ("0" * 4300, 10),
              ("f" * 9998, 16), ("1" * 40000, 2),
              ("", 10), ("", 16), ("12a", 10), ("129", 8), ("102", 2), ("g", 16), ("1.5", 10),
              ("-", 10), ("x", 16), ("1" * 4301 + "x", 10)]
    for s, radix in texts:
        for negative in (False, True):
            w.row("parse", s, radix, negative,
                  res(lambda: int(("-" if negative else "") + s, radix)))
    for k in list(range(-3, 30)) + [50, 100, 169, 170, 171, 300, 1000] + w.pick([], [3000, 10000]):
        w.row("fact", k, res(math.factorial, k))


def gen_ratio(w):
    rng = w.rng
    n = w.pick(quick=300, full=20000)
    pairs = []
    for _ in range(n):
        a = rand_int(rng, rng.choice((60, 300, 1200, 2300)))
        b = rand_int(rng, rng.choice((60, 300, 1200, 2300)))
        pairs.append((a, b))
    for _ in range(n // 2):
        # exact ties: q / 2 with q odd (54 bits), scaled by a common factor
        q = rng.getrandbits(54) | (1 << 53) | 1
        d = rand_int(rng, 400) or 1
        s = rng.randint(-1100, 1000)
        num, den = q * d, 2 * d
        if s >= 0:
            num <<= s
        else:
            den <<= -s
        pairs.append((num, den))
        # subnormal ties: (2m + 1) * 2**-1075
        m = rng.getrandbits(rng.randint(1, 52))
        pairs.append(((2 * m + 1) * d, (1 << 1075) * d))
        pairs.append(((2 * m + 1) * d + 1, (1 << 1075) * d))
    top = (1 << 1024) - (1 << 970)
    for num, den in ((top, 1), (top - 1, 1), (top * 3, 3), (top * 3 - 1, 3), (1, 1 << 1074),
                     (1, 1 << 1075), (1, (1 << 1075) - 1), (3, 1 << 1076), (1, 1 << 1076),
                     (0, 5), (0, -5), (5, 0), (0, 0), (-1, 1 << 2000), (1 << 2000, 1),
                     (-(1 << 2000), 3), (1 << 1100, 1 << 76), (1 << 1100, (1 << 76) + 1),
                     (0x20000000000001 * 3, 3), (10 ** 400, 10 ** 399), (1, 3), (-2, 3)):
        pairs.append((num, den))
    for num, den in pairs:
        r = vecutil.result(operator.truediv, num, den)
        w.row("ratio", num, den, True, r)
        if r == vecutil.Exc("OverflowError"):
            r = INF if (num < 0) == (den < 0) else -INF
        w.row("ratio", num, den, False, r)


def gen_float_ops(w):
    rng = w.rng
    n = w.pick(quick=300, full=20000)
    pairs = [(a, b) for a in SPECIAL_FLOATS for b in SPECIAL_FLOATS]
    for _ in range(n):
        pairs.append((rand_float(rng), rand_float(rng)))
    for x, y in pairs:
        w.row("frem", x, y, res(operator.mod, x, y))
        w.row("fpow", x, y, res(operator.pow, x, y))
    for _ in range(n):
        x = vecutil.rand_double(rng) if rng.random() < 0.5 else rng.uniform(-1e3, 1e3)
        for y in (2.0, 3.0, 0.5, 1 / 3, -1.0, -2.0):
            w.row("fpow", x, y, res(operator.pow, x, y))
    # x ** 2.0 where the platform pow() differs from x * x (about 1 double in 1,500 on glibc):
    # a build that lets the compiler turn pow(x, 2.0) into x * x fails these (DESIGN.md 5.1)
    found = 0
    for _ in range(10 ** 6):
        if found == w.pick(8, 60):
            break
        x = rng.uniform(-1e3, 1e3)
        if x ** 2.0 != x * x:
            w.row("fpow", x, 2.0, x ** 2.0)
            found += 1
    for _ in range(n // 4):
        # negative bases with fractional exponents: complex, or OverflowError on overflow
        x = -abs(rand_float(rng)) or -1.5
        y = rng.choice((0.5, 1.5, 2.5, -0.5, 1 / 3, 7.25, rng.uniform(-400, 400)))
        x2 = -10.0 ** rng.randint(1, 308)
        x3 = -rng.uniform(1.0001, 3)
        y3 = rng.randint(-2000, 2000) + 0.5
        for a, b in ((x, y), (x2, y), (x3, y3)):
            w.row("fpow", a, b, res(operator.pow, a, b))


def gen_math(w):
    rng = w.rng
    n = w.pick(quick=30, full=5000)
    xs = vecutil.edge_doubles()[:: w.pick(3, 1)] + SPECIAL_FLOATS
    for _ in range(n):
        xs += [rand_float(rng), rng.uniform(-1.2, 1.2), rng.uniform(-720, 720),
               float(rng.randint(-1000, 1000) * 90)]
    for name in ("sqrt", "log", "log10", "log2", "sin", "cos", "tan", "asin", "acos", "atan",
                 "degrees", "radians"):
        fn = getattr(math, name)
        for x in xs:
            w.row("math", name, x, res(fn, x))
    pool = SPECIAL_FLOATS + [rand_float(rng) for _ in range(w.pick(10, 1000))]
    for x in pool:
        for y in SPECIAL_FLOATS + [rand_float(rng) for _ in range(3)]:
            w.row("math2", "fmod", x, y, res(math.fmod, x, y))
            w.row("math2", "copysign", x, y, res(math.copysign, x, y))


def main():
    with vecutil.Writer("numbers") as w:
        gen_ops(w)
        gen_unary(w)
        gen_cmp(w)
        gen_text(w)
        gen_ratio(w)
        gen_float_ops(w)
        gen_math(w)


if __name__ == "__main__":
    main()
